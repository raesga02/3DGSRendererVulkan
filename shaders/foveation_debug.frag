#version 450

layout(push_constant) uniform Push {
    vec2 viewport;          // target extent (unused, kept for debugging)
    vec2 texelSize;         // rate-map texel size in pixels
    vec2 foveaCenterPx;     // optical-axis centre for this eye
    float halfExtent;       // 0.5 * min(viewport)
    float foveaRadius;      // normalised 1x1 radius
    float midRadius;        // normalised 2x2 radius
    int peripheralRate;     // 2 or 4
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Match the rate-map texel grid exactly (16x16 by default).
    vec2 texelCenter = (floor(gl_FragCoord.xy / pc.texelSize) + 0.5) * pc.texelSize;
    float r = length(texelCenter - pc.foveaCenterPx) / pc.halfExtent;

    vec3 color;
    if (r <= pc.foveaRadius) {
        color = vec3(0.10, 1.00, 0.10);        // 1x1
    } else if (r <= pc.midRadius || pc.peripheralRate < 4) {
        color = vec3(1.00, 0.85, 0.10);        // 2x2
    } else {
        color = vec3(1.00, 0.15, 0.15);        // 4x4
    }

    outColor = vec4(color, 0.35);
}
