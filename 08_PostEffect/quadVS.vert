#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec2 inUV;

layout(location=0) out vec2 outUV;

out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  float x = float(gl_VertexIndex % 2);
  float y = float(gl_VertexIndex / 2);

  gl_Position = vec4(
	 2.0*x-1,
	 2.0*y-1,
	0,1);
  outUV = vec2(x,y);
}
