#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vOpacity;
layout(location = 2) in vec2 vCenter;
layout(location = 3) in vec3 vConic;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 proj;
    vec4 viewport;        // width, height, 1/width, 1/height
    vec4 viewportOrigin;  // x, y offset of the view within the target
    vec4 cameraPos;
} camera;

void main() {
    vec2 centerPix = (vCenter * 0.5 + 0.5) * camera.viewport.xy + camera.viewportOrigin.xy;
    vec2 d = gl_FragCoord.xy - centerPix;

    float a = vConic.x;
    float b = vConic.y;
    float c = vConic.z;

    float power = -0.5 * (a * d.x * d.x + 2.0 * b * d.x * d.y + c * d.y * d.y);
    if (power > 0.0)
        discard;

    float alpha = exp(power) * vOpacity;
    if (alpha < 1.0 / 255.0)
        discard;

    outColor = vec4(vColor, alpha);
}
