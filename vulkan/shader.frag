#version 460
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 o_color;
void main() {
    o_color = vec4(v_color.bgr, 1.); // need to swizzle the color channels since we're rendering to an image with BGRA pixel format instead of RGBA
}
