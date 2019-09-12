#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec2 inUV;

layout(location=0) out vec2 outUV;

out gl_PerVertex
{
  vec4 gl_Position;
};

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  world;
  mat4  view;
  mat4  proj;
};

void main()
{
  gl_Position = proj * view * world * inPos;
  outUV = inUV;
}
