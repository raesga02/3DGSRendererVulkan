#version 450

// Instanced-quad 3D Gaussian splatting (EWA projection + view-dependent SH).

struct Gaussian {
    float position[3];
    float cov3D[6];        // xx, xy, xz, yy, yz, zz
    float opacity;
    float sh[48];          // [coeff][channel], coeff 0 = DC
};

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 proj;
    vec4 viewport;        // width, height, 1/width, 1/height
    vec4 cameraPos;
} camera;

layout(set = 0, binding = 1, std430) readonly buffer GaussianBuffer {
    Gaussian gaussians[];
};

layout(set = 0, binding = 2, std430) readonly buffer IndexBuffer {
    uint sortedIndices[];
};

layout(push_constant) uniform Push {
    float splatScale;
    float maxRadiusFraction;        // 0 disables the floater filter
    int shDegree;
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vOpacity;
layout(location = 2) out vec2 vCenter;
layout(location = 3) out vec3 vConic;

// Spherical-harmonics constants (INRIA 3DGS reference).
const float SH_C0 = 0.28209479177387814;
const float SH_C1 = 0.4886025119029199;
const float SH_C2_0 = 1.0925484305920792;
const float SH_C2_1 = -1.0925484305920792;
const float SH_C2_2 = 0.31539156525252005;
const float SH_C2_3 = -1.0925484305920792;
const float SH_C2_4 = 0.5462742152960396;
const float SH_C3_0 = -0.5900435899266435;
const float SH_C3_1 = 2.890611442640554;
const float SH_C3_2 = -0.4570457994644658;
const float SH_C3_3 = 0.3731763325901154;
const float SH_C3_4 = -0.4570457994644658;
const float SH_C3_5 = 1.445305721320277;
const float SH_C3_6 = -0.5900435899266435;

vec3 evalColor(Gaussian g, vec3 dir) {
    vec3 result = SH_C0 * vec3(g.sh[0], g.sh[1], g.sh[2]);
    if (pc.shDegree > 0) {
        float x = dir.x, y = dir.y, z = dir.z;
        result += -SH_C1 * y * vec3(g.sh[3], g.sh[4], g.sh[5])
                +  SH_C1 * z * vec3(g.sh[6], g.sh[7], g.sh[8])
                -  SH_C1 * x * vec3(g.sh[9], g.sh[10], g.sh[11]);
        if (pc.shDegree > 1) {
            float xx = x * x, yy = y * y, zz = z * z;
            float xy = x * y, yz = y * z, xz = x * z;
            result += SH_C2_0 * xy * vec3(g.sh[12], g.sh[13], g.sh[14])
                    + SH_C2_1 * yz * vec3(g.sh[15], g.sh[16], g.sh[17])
                    + SH_C2_2 * (2.0 * zz - xx - yy) * vec3(g.sh[18], g.sh[19], g.sh[20])
                    + SH_C2_3 * xz * vec3(g.sh[21], g.sh[22], g.sh[23])
                    + SH_C2_4 * (xx - yy) * vec3(g.sh[24], g.sh[25], g.sh[26]);
            if (pc.shDegree > 2) {
                result += SH_C3_0 * y * (3.0 * xx - yy) * vec3(g.sh[27], g.sh[28], g.sh[29])
                        + SH_C3_1 * xy * z * vec3(g.sh[30], g.sh[31], g.sh[32])
                        + SH_C3_2 * y * (4.0 * zz - xx - yy) * vec3(g.sh[33], g.sh[34], g.sh[35])
                        + SH_C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * vec3(g.sh[36], g.sh[37], g.sh[38])
                        + SH_C3_4 * x * (4.0 * zz - xx - yy) * vec3(g.sh[39], g.sh[40], g.sh[41])
                        + SH_C3_5 * z * (xx - yy) * vec3(g.sh[42], g.sh[43], g.sh[44])
                        + SH_C3_6 * x * (xx - 3.0 * yy) * vec3(g.sh[45], g.sh[46], g.sh[47]);
            }
        }
    }
    return max(result + 0.5, vec3(0.0));
}

void main() {
    uint gi = sortedIndices[gl_InstanceIndex];
    Gaussian g = gaussians[gi];

    vec3 worldPos = vec3(g.position[0], g.position[1], g.position[2]);
    vec4 viewPos4 = camera.view * vec4(worldPos, 1.0);
    vec3 viewPos = viewPos4.xyz;

    // Cull splats behind (or too close to) the camera.
    if (viewPos.z > -0.05) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vColor = vec3(0.0);
        vOpacity = 0.0;
        vCenter = vec2(0.0);
        vConic = vec3(0.0);
        return;
    }

    mat3 Vrk = mat3(
        g.cov3D[0], g.cov3D[1], g.cov3D[2],
        g.cov3D[1], g.cov3D[3], g.cov3D[4],
        g.cov3D[2], g.cov3D[4], g.cov3D[5]);

    float focalX = 0.5 * camera.viewport.x * camera.proj[0][0];
    float focalY = 0.5 * camera.viewport.y * camera.proj[1][1];

    float invZ = 1.0 / viewPos.z;
    // Jacobian of the perspective projection (column-major mat3).
    mat3 J = mat3(
        focalX * invZ, 0.0, 0.0,
        0.0, focalY * invZ, 0.0,
        -focalX * viewPos.x * invZ * invZ, -focalY * viewPos.y * invZ * invZ, 0.0);

    mat3 W = mat3(camera.view);
    mat3 T = J * W;
    mat3 cov2D = T * Vrk * transpose(T);

    float scale2 = pc.splatScale * pc.splatScale;
    float a = cov2D[0][0] * scale2 + 0.3;
    float b = cov2D[0][1] * scale2;
    float c = cov2D[1][1] * scale2 + 0.3;

    float det = max(a * c - b * b, 1e-6);
    float invDet = 1.0 / det;
    vec3 conic = vec3(c * invDet, -b * invDet, a * invDet);

    vec4 clip = camera.proj * viewPos4;
    vec3 ndc = clip.xyz / clip.w;
    vCenter = ndc.xy;

    // Quad half-size = 4 sigma (major axis) in pixels -> NDC.
    float mid = 0.5 * (a + c);
    float term = sqrt(max(0.1, mid * mid - (a * c - b * b)));
    float radiusPx = 4.0 * sqrt(max(mid + term, 1e-4));

    // Floater filter: drop Gaussians whose screen footprint is absurdly large.
    if (pc.maxRadiusFraction > 0.0) {
        float maxRadiusPx = pc.maxRadiusFraction * min(camera.viewport.x, camera.viewport.y);
        if (radiusPx > maxRadiusPx) {
            gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
            vColor = vec3(0.0);
            vOpacity = 0.0;
            vCenter = vec2(0.0);
            vConic = vec3(0.0);
            return;
        }
    }

    vec2 radiusNdc = vec2(radiusPx * 2.0 * camera.viewport.z,
                          radiusPx * 2.0 * camera.viewport.w);

    vec2 quad[6] = vec2[](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
        vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));
    vec2 corner = quad[gl_VertexIndex];

    gl_Position = vec4(ndc.xy + corner * radiusNdc, ndc.z, 1.0);

    vec3 dir = normalize(worldPos - camera.cameraPos.xyz);
    vColor = evalColor(g, dir);
    vOpacity = g.opacity;
    vConic = conic;
}
