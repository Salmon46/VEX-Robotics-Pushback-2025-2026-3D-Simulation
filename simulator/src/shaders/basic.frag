#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    // Directional light from upper-right-front
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float ambient = 0.25;

    // Diffuse (half-Lambert for softer shading)
    float NdotL = dot(normalize(fragNormal), lightDir);
    float diffuse = NdotL * 0.5 + 0.5; // half-Lambert: [0,1] range

    float lighting = ambient + (1.0 - ambient) * diffuse;

    outColor = vec4(fragColor * lighting, 1.0);
}
