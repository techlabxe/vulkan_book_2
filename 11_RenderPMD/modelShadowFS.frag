#version 450

layout(location=0) in vec4 inColor;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform SceneParameter
{
  mat4  view;
  mat4  proj;
  vec4  lightDirection;
  vec4  eyePosition;
  vec4  outlineColor;
  mat4  lightViewProj;
  mat4  lightViewProjBias;
};


layout(set=0, binding=2)
uniform MaterialParameter
{
  vec4 diffuse;
  vec4 ambient;
  vec4 specular;
  uint useTexture;
};

void main()
{
  float d = inColor.z / inColor.w;
  outColor = vec4(d,d,d,1);
}
