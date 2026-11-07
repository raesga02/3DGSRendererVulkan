#version 450

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    vec2 viewport;          // offscreen source extent
    vec2 viewOffset;        // target render-area offset
    vec2 foveaCenterPx;     // optical-axis centre in source pixels
    float halfExtent;       // 0.5 * min(viewport)
    float foveaRadius;      // normalised end of the sharp region
    float ringWidth;        // normalised width of the transition ring
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 targetPx = ivec2(gl_FragCoord.xy);
    ivec2 localPx = targetPx - ivec2(pc.viewOffset);
    ivec2 maxPx = ivec2(pc.viewport) - ivec2(1);
    localPx = clamp(localPx, ivec2(0), maxPx);

    vec3 sharp = texelFetch(uScene, localPx, 0).rgb;

    vec2 d = (vec2(localPx) + 0.5 - pc.foveaCenterPx) / pc.halfExtent;
    float r = length(d);
    float t = clamp((r - pc.foveaRadius) / max(pc.ringWidth, 1e-4), 0.0, 1.0);
    float mask = t * t * (3.0 - 2.0 * t);

    // Fovea: no blur, early out before the 3x3 taps.
    if (mask <= 0.001) {
        outColor = vec4(sharp, 1.0);
        return;
    }

    const float weights[9] = float[](
        1.0, 2.0, 1.0,
        2.0, 4.0, 2.0,
        1.0, 2.0, 1.0);

    vec3 blur = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 c = clamp(localPx + ivec2(dx, dy), ivec2(0), maxPx);
            blur += texelFetch(uScene, c, 0).rgb * weights[(dy + 1) * 3 + (dx + 1)];
        }
    }
    blur /= 16.0;

    outColor = vec4(mix(sharp, blur, mask), 1.0);
}
