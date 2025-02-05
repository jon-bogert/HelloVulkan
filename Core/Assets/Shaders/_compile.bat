@echo off

glslangValidator -V -o bin\\basic.vert.spirv basic.vert.glsl
glslangValidator -V -o bin\\basic.frag.spirv basic.frag.glsl

pause