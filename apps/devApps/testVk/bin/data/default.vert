#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms (resources)
layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
}; // note: if you don't specify a variable name for the block its elements will live in the global namespace.

layout (set = 0, binding = 1) uniform Style
{
	vec4 globalColor;
} style;

// inputs (vertex attributes)
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec2 inTexCoord;

// outputs 
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outTexCoord;

// we override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
	outNormal    = (inverse(transpose( viewMatrix * modelMatrix)) * vec4(inNormal, 0.0)).xyz;
	outColor     = style.globalColor;
	outTexCoord = inTexCoord;
	gl_Position  = projectionMatrix * viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
}
