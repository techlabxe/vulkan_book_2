#version 450

layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=1)
uniform sampler2D texRendered;

void main()
{
  outColor = texture(texRendered, inUV);
//outColor = vec4(inUV.xy, 0, 1);
}
