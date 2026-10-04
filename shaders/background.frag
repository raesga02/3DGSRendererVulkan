#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    vec4 top;
    vec4 bottom;
} pc;

void main() {
    outColor = vec4(mix(pc.bottom.rgb, pc.top.rgb, uv.y), 1.0);
}
