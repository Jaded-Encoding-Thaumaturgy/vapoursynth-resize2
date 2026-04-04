project('resize2', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'c_std=c89', 'cpp_std=c++17'],
  meson_version : '>=1.0.0',
  version : '0.4.0'
)

sources = [
    'resize.cpp'
]

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

deps = [
    vapoursynth_dep,
    dependency('zimg_patched'),
]

cxx = meson.get_compiler('cpp')
py = import('python').find_installation()

# GH-18: Prefer internal, patched zimg symbols over any external ones.
add_project_link_arguments(cxx.get_supported_link_arguments('-Wl,-Bsymbolic-functions'), language : 'cpp')

shared_module('resize2', sources,
    dependencies: deps,
    link_args: get_option('link-static') ? ['-static'] : [],
    install: true,
    install_dir: py.get_install_dir() / 'vapoursynth' / 'plugins',
    gnu_symbol_visibility: 'hidden',
)

# Force platform tag.
# We install a dummy file into the root of the wheel data (not site-packages).
# The presence of a file in a non-pure path forces meson-python to give the
# wheel a platform tag without forcing the Python ABI tag.
tag_file = configure_file(
    output: 'resize2.tag',
    configuration: configuration_data(),
)

install_data(
    tag_file,
    install_dir: get_option('libdir')
)