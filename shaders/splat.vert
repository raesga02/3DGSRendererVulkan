#version 450

// Instanced-quad 3D Gaussian splatting (EWA projection).
//
// One instance per visible Gaussian. The 3D covariance is precomputed on the
// CPU; here we project it to screen space, derive the 2D conic, and emit a
// billboard quad large enough to cover the Gaussian.

struct Gaussian {
    float position[3];
    float cov3D[6];        // xx, xy, xz, yy, yz, zz
    float color[3];
    float opacity;
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
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vOpacity;
layout(location = 2) out vec2 vCenter;
layout(location = 3) out vec3 vConic;

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
    // J = [ fx/z   0    -fx*x/z^2 ;
    //        0    fy/z  -fy*y/z^2 ;
    //        0     0        0     ]
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

    // Floater filter: drop Gaussians whose screen footprint is absurdly large
    // (sky/background blobs), which otherwise wash out the view from outside.
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

    vColor = vec3(g.color[0], g.color[1], g.color[2]);
    vOpacity = g.opacity;
    vConic = conic;
}
