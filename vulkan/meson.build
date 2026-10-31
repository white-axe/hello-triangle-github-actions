project('triangle-vulkan', 'c', meson_version: '>=1.3.0', version: '0.1')

cmake = import('cmake')

c_std = 'c99'
cmake_c_std = '99'

cmake_base_options = {
  'CMAKE_POLICY_VERSION_MINIMUM': '3.10',
  'CMAKE_OSX_SYSROOT': '/nonexistent',
  'CMAKE_C_STANDARD': cmake_c_std,
  'CMAKE_C_BYTE_ORDER': host_machine.endian() == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
  'CMAKE_OBJC_BYTE_ORDER': host_machine.endian() == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
  'CMAKE_BUILD_TYPE': 'Custom',
}

meson_base_options = {
  'default_library': 'static',
  'c_std': c_std,
}

volk = subproject('volk', default_options: meson_base_options).get_variable('volk')

glfw_options = cmake.subproject_options()
glfw_options.add_cmake_defines(cmake_base_options + {
  'BUILD_SHARED_LIBS': false,
  'GLFW_BUILD_EXAMPLES': false,
  'GLFW_BUILD_TESTS': false,
  'GLFW_BUILD_DOCS': false,
  'GLFW_INSTALL': false,
})
glfw = cmake.subproject('glfw', options: glfw_options).dependency('glfw')
if host_machine.system() == 'darwin'
  glfw = declare_dependency(
    dependencies: [
      glfw,
      dependency(
        'appleframeworks',
        modules: [
          'Cocoa',
          'CoreFoundation',
          'IOKit',
          'QuartzCore',
        ],
      ),
    ],
  )
endif

stb = subproject('stb', default_options: meson_base_options).get_variable('stb')

executable(
  'triangle',
  override_options: {
    'c_std': c_std,
  },
  dependencies: [
    volk,
    glfw,
    stb,
    declare_dependency(
      sources: [
        custom_target(
          'shader.vert.spv.h',
          output: 'shader.vert.spv.h',
          command: [
            import('python').find_installation(),
            files('embedtool.py'),
            custom_target(
              'shader.vert.spv',
              output: 'shader.vert.spv',
              command: [
                find_program('glslc'),
                files('shader.vert'),
                '-o', '@OUTPUT@',
              ],
            ),
            '@OUTPUT@',
            'shader_vert_spv',
          ],
        ),
        custom_target(
          'shader.frag.spv.h',
          output: 'shader.frag.spv.h',
          command: [
            import('python').find_installation(),
            files('embedtool.py'),
            custom_target(
              'shader.frag.spv',
              output: 'shader.frag.spv',
              command: [
                find_program('glslc'),
                files('shader.frag'),
                '-o', '@OUTPUT@',
              ],
            ),
            '@OUTPUT@',
            'shader_frag_spv',
          ],
        ),
      ],
    ),
  ],
  sources: [
    'main.c',
  ],
  build_rpath: meson.get_compiler('c').get_linker_id().split('.')[0] == 'ld64' ? '@loader_path' : '$ORIGIN',
)
