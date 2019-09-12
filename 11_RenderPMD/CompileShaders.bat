@echo off
glslangValidator -V -S vert modelVS.vert -o modelVS.spv
glslangValidator -V -S frag modelFS.frag -o modelFS.spv

glslangValidator -V -S vert modelOutlineVS.vert -o modelOutlineVS.spv
glslangValidator -V -S frag modelOutlineFS.frag -o modelOutlineFS.spv

glslangValidator -V -S vert modelShadowVS.vert -o modelShadowVS.spv
glslangValidator -V -S frag modelShadowFS.frag -o modelShadowFS.spv

@echo on
