project('stb', 'c', meson_version: '>=1.3.0')

glad = declare_dependency(
  include_directories: [
    'include',
  ],
  link_with: [
    static_library(
      'glad',
      include_directories: [
        'include',
      ],
      sources: [
        'src/glad.c',
      ],
    )
  ],
)
