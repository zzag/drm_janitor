project(
    'drm_janitor',
    'c',
    license: 'MIT',
    meson_version: '>=0.49.0',
    default_options: [
        'c_std=c11',
        'warning_level=2',
    ],
)

cc = meson.get_compiler('c')

add_project_arguments([
    '-D_POSIX_C_SOURCE=200809L',
], language: 'c')

add_project_arguments(cc.get_supported_arguments([
    '-Wno-missing-braces',
    '-Wno-missing-field-initializers',
    '-Wno-unused-parameter',
]), language: 'c')

libdrm = dependency('libdrm')
wayland_server = dependency('wayland-server')
wlroots = dependency('wlroots')

executable(
    'drm_janitor',
    files('main.c'),
    dependencies: [libdrm, wayland_server, wlroots],
    install: true,
)

