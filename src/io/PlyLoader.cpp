#include "PlyLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace io {

namespace {

struct Property {
	std::string name;
	int offset = 0;
	int size = 4;
	bool isFloat = false;
};

int typeSize(const std::string &type) {
	if (type == "char" || type == "uchar" || type == "int8" || type == "uint8")
		return 1;
	if (type == "short" || type == "ushort" || type == "int16" || type == "uint16")
		return 2;
	if (type == "int" || type == "uint" || type == "int32" || type == "uint32" ||
	    type == "float" || type == "float32")
		return 4;
	if (type == "double" || type == "float64")
		return 8;
	throw std::runtime_error("Unsupported PLY property type: " + type);
}

float readFloat(const char *base, const Property &prop) {
	if (prop.isFloat) {
		float v = 0.0f;
		std::memcpy(&v, base + prop.offset, sizeof(float));
		return v;
	}
	if (prop.size == 8) {
		double v = 0.0;
		std::memcpy(&v, base + prop.offset, sizeof(double));
		return static_cast<float>(v);
	}
	if (prop.size == 4) {
		int32_t v = 0;
		std::memcpy(&v, base + prop.offset, sizeof(int32_t));
		return static_cast<float>(v);
	}
	if (prop.size == 2) {
		int16_t v = 0;
		std::memcpy(&v, base + prop.offset, sizeof(int16_t));
		return static_cast<float>(v);
	}
	uint8_t v = 0;
	std::memcpy(&v, base + prop.offset, sizeof(uint8_t));
	return static_cast<float>(v);
}

}        // namespace

std::vector<scene::GaussianData> PlyLoader::load(const std::string &path, SceneMetadata &outMeta) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("Failed to open PLY file: " + path);

	// --- Parse ASCII header -------------------------------------------------
	std::string format;
	uint64_t vertexCount = 0;
	std::vector<Property> properties;
	int stride = 0;

	std::string line;
	bool inVertexElement = false;
	bool headerDone = false;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		std::istringstream ss(line);
		std::string token;
		ss >> token;

		if (token == "format") {
			ss >> format;
		} else if (token == "element") {
			std::string elementName;
			ss >> elementName;
			uint64_t count = 0;
			ss >> count;
			inVertexElement = (elementName == "vertex");
			if (inVertexElement)
				vertexCount = count;
		} else if (token == "property" && inVertexElement) {
			std::string type;
			std::string name;
			ss >> type >> name;
			Property prop;
			prop.name = name;
			prop.size = typeSize(type);
			prop.isFloat = (type == "float" || type == "float32");
			prop.offset = stride;
			stride += prop.size;
			properties.push_back(prop);
		} else if (token == "end_header") {
			headerDone = true;
			break;
		}
	}

	if (!headerDone)
		throw std::runtime_error("Malformed PLY (no end_header): " + path);
	if (format != "binary_little_endian")
		throw std::runtime_error("Only binary_little_endian PLY is supported: " + path);
	if (vertexCount == 0 || stride == 0)
		throw std::runtime_error("PLY has no vertex element: " + path);

	// --- Read the vertex body in blocks ------------------------------------
	std::vector<scene::GaussianData> gaussians(vertexCount);
	outMeta.aabbMin = glm::vec3(std::numeric_limits<float>::max());
	outMeta.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

	constexpr size_t kBlockVertices = 8192;
	std::vector<char> block(kBlockVertices * static_cast<size_t>(stride));

	int maxRestIndex = -1;
	uint64_t remaining = vertexCount;
	uint64_t base = 0;

	while (remaining > 0) {
		size_t batch = static_cast<size_t>(std::min<uint64_t>(remaining, kBlockVertices));
		file.read(block.data(), static_cast<std::streamsize>(batch * stride));
		if (!file)
			throw std::runtime_error("Unexpected end of PLY vertex data: " + path);

		for (size_t v = 0; v < batch; ++v) {
			const char *rec = block.data() + v * static_cast<size_t>(stride);
			scene::GaussianData &g = gaussians[base + v];

			for (const Property &prop : properties) {
				const std::string &n = prop.name;
				float value = readFloat(rec, prop);

				if (n == "x")
					g.position[0] = value;
				else if (n == "y")
					g.position[1] = value;
				else if (n == "z")
					g.position[2] = value;
				else if (n == "nx")
					g.normal[0] = value;
				else if (n == "ny")
					g.normal[1] = value;
				else if (n == "nz")
					g.normal[2] = value;
				else if (n == "opacity")
					g.opacity = value;
				else if (n == "scale_0")
					g.scale[0] = value;
				else if (n == "scale_1")
					g.scale[1] = value;
				else if (n == "scale_2")
					g.scale[2] = value;
				else if (n == "rot_0")
					g.rotation[0] = value;
				else if (n == "rot_1")
					g.rotation[1] = value;
				else if (n == "rot_2")
					g.rotation[2] = value;
				else if (n == "rot_3")
					g.rotation[3] = value;
				else if (n.rfind("f_dc_", 0) == 0) {
					int idx = std::stoi(n.substr(5));
					if (idx >= 0 && idx < 3)
						g.f_dc[idx] = value;
				} else if (n.rfind("f_rest_", 0) == 0) {
					int idx = std::stoi(n.substr(7));
					if (idx >= 0 && idx < 45) {
						g.f_rest[idx] = value;
						maxRestIndex = std::max(maxRestIndex, idx);
					}
				}
			}

			glm::vec3 p(g.position[0], g.position[1], g.position[2]);
			outMeta.aabbMin = glm::min(outMeta.aabbMin, p);
			outMeta.aabbMax = glm::max(outMeta.aabbMax, p);
		}

		base += batch;
		remaining -= batch;
	}

	// --- Derive SH degree from the number of higher-order coefficients -----
	outMeta.count = static_cast<uint32_t>(vertexCount);
	int restCount = maxRestIndex + 1;
	if (restCount >= 3) {
		int coeffsPerChannel = restCount / 3 + 1;        // + DC
		int degree = static_cast<int>(std::lround(std::sqrt(static_cast<double>(coeffsPerChannel)))) - 1;
		outMeta.shDegree = std::max(0, std::min(3, degree));
	} else {
		outMeta.shDegree = 0;
	}

	// --- Robust framing (median + 90th percentile distance) ----------------
	{
		std::vector<float> axis[3];
		for (int a = 0; a < 3; ++a)
			axis[a].resize(vertexCount);
		for (uint64_t i = 0; i < vertexCount; ++i) {
			axis[0][i] = gaussians[i].position[0];
			axis[1][i] = gaussians[i].position[1];
			axis[2][i] = gaussians[i].position[2];
		}

		auto medianOf = [](std::vector<float> &v) {
			size_t mid = v.size() / 2;
			std::nth_element(v.begin(), v.begin() + mid, v.end());
			return v[mid];
		};

		outMeta.center = glm::vec3(medianOf(axis[0]), medianOf(axis[1]), medianOf(axis[2]));

		std::vector<float> distances(vertexCount);
		for (uint64_t i = 0; i < vertexCount; ++i) {
			glm::vec3 p(gaussians[i].position[0], gaussians[i].position[1], gaussians[i].position[2]);
			distances[i] = glm::length(p - outMeta.center);
		}
		size_t k = std::min(vertexCount - 1, static_cast<size_t>(0.9 * static_cast<double>(vertexCount)));
		std::nth_element(distances.begin(), distances.begin() + k, distances.end());
		outMeta.radius = std::max(distances[k], 1e-3f);
	}

	return gaussians;
}

}        // namespace io
