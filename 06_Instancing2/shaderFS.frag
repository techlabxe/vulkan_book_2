#version 450

layout(location=0) in vec4 inColor;

layout(location=0) out vec4 outColor;

const mat3x3 from709to2020 = {
  { 0.6274040, 0.3292820, 0.0433136 },
  { 0.0690970, 0.9195400, 0.0113612 },
  { 0.0163916, 0.0880132, 0.8955950 }
}; 

vec3 LinearToST2084(vec3 val)
{
  float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875; 
  float m1 = 0.1593017578, m2 = 78.84375; 
  vec3 L = abs(val);
  vec3 a = c1 + c2 * pow(L, vec3(m1,m1,m1));
  vec3 b = 1.0 + c3 * pow(L, vec3(m1,m1,m1));
  vec3 st2084 = pow( a / b, vec3(m2,m2,m2));
  return st2084;
}

void main()
{
  outColor = inColor;
}
