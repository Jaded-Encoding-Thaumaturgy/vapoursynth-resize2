project('zimg', 'cpp',
  default_options : ['c_std=c89', 'cpp_std=c++17'],
  meson_version : '>=0.51.0',
  version : '3.0.5'
)

cxx = meson.get_compiler('cpp')

compile_args = [
    '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Wno-format', '-Wno-endif-labels',
    '-lstdc++', '-fvisibility=hidden', '-DGRAPHENGINE_IMPL_NAMESPACE=zimg'
]

host_cpu_family = host_machine.cpu_family()
host_system = host_machine.system()

opt_ARMSIMD = false
opt_X86SIMD = false
opt_X86SIMD_AVX512 = false

link_libs = []

if host_cpu_family in ['arm', 'aarch64']
    opt_ARMSIMD = get_option('ENABLE_ARMSIMD')
    compile_args += ['-DZIMG_ARM']
elif host_cpu_family.startswith('x86')
    opt_X86SIMD = get_option('ENABLE_X86SIMD')


    code = '''
#include <immintrin.h>
int main()
{
	volatile __m512 x = _mm512_setzero_ps();
	x = _mm512_add_ps(x, x);
#if defined(__GNUC__) && defined(__x86_64__)
	asm volatile ("" : : : "zmm16", "zmm17", "zmm18", "zmm19");
#endif
	return 0;
}
    '''
    result = cxx.compiles(code, args : ['-mavx512f', '-mavx512pf', '-mavx512er', '-mavx512cd', '-mavx512vl', '-mavx512bw', '-mavx512dq', '-mavx512ifma', '-mavx512vbmi', '-mavx512vbmi2', '-mavx512bitalg', '-mavx512vpopcntdq', '-mavx512vnni'], name : 'avx512 checking')

    opt_X86SIMD_AVX512 = result and get_option('ENABLE_X86SIMD_AVX512')

    if opt_X86SIMD_AVX512
        compile_args += ['-DZIMG_X86_AVX512']
    endif

    if opt_X86SIMD or opt_X86SIMD_AVX512
        compile_args += ['-DZIMG_X86']
    endif

    compile_args += ['-mfpmath=sse', '-msse2']
endif



incl_dirs = include_directories(
    'graphengine/include',
    'src/zimg',
    'src',
)

sources = [
    'graphengine/graphengine/cpuinfo.h',
    'graphengine/graphengine/cpuinfo.cpp',
    'graphengine/graphengine/graph.cpp',
    'graphengine/graphengine/node.cpp',
    'graphengine/graphengine/node.h',
    'graphengine/graphengine/state.h',
    'graphengine/graphengine/x86/cpuinfo_x86.cpp',
    'graphengine/graphengine/x86/cpuinfo_x86.h',
    'graphengine/include/graphengine/filter.h',
    'graphengine/include/graphengine/graph.h',
    'graphengine/include/graphengine/namespace.h',
    'graphengine/include/graphengine/types.h',

    'src/zimg/api/zimg.cpp',
    'src/zimg/api/zimg.h',
    'src/zimg/api/zimg++.hpp',

    'src/zimg/colorspace/colorspace.cpp',
    'src/zimg/colorspace/colorspace.h',
    'src/zimg/colorspace/colorspace_graph.cpp',
    'src/zimg/colorspace/colorspace_graph.h',
    'src/zimg/colorspace/colorspace_param.cpp',
    'src/zimg/colorspace/colorspace_param.h',

    'src/zimg/colorspace/gamma.cpp',
    'src/zimg/colorspace/gamma.h',
    'src/zimg/colorspace/matrix3.cpp',
    'src/zimg/colorspace/matrix3.h',
    'src/zimg/colorspace/operation.cpp',
    'src/zimg/colorspace/operation.h',
    'src/zimg/colorspace/operation_impl.cpp',
    'src/zimg/colorspace/operation_impl.h',

    'src/zimg/common/align.h',
    'src/zimg/common/alloc.h',
    'src/zimg/common/builder.h',
    'src/zimg/common/ccdep.h',
    'src/zimg/common/checked_int.h',
    'src/zimg/common/cpuinfo.cpp',
    'src/zimg/common/cpuinfo.h',
    'src/zimg/common/except.h',
    'src/zimg/common/libm_wrapper.h',
    'src/zimg/common/libm_wrapper.cpp',
    'src/zimg/common/make_array.h',
    'src/zimg/common/matrix.cpp',
    'src/zimg/common/matrix.h',
    'src/zimg/common/pixel.h',
    'src/zimg/common/static_map.h',
    'src/zimg/common/unroll.h',
    'src/zimg/common/zassert.h',

    'src/zimg/depth/blue.cpp',
    'src/zimg/depth/blue.h',
    'src/zimg/depth/depth_convert.cpp',
    'src/zimg/depth/depth_convert.h',
    'src/zimg/depth/depth.cpp',
    'src/zimg/depth/depth.h',
    'src/zimg/depth/dither.cpp',
    'src/zimg/depth/dither.h',
    'src/zimg/depth/quantize.h',
    'src/zimg/depth/quantize.cpp',

    'src/zimg/graph/filter_base.cpp',
    'src/zimg/graph/filter_base.h',
    'src/zimg/graph/filtergraph.cpp',
    'src/zimg/graph/filtergraph.h',
    'src/zimg/graph/graphbuilder.cpp',
    'src/zimg/graph/graphbuilder.h',
    'src/zimg/graph/graphengine_except.cpp',
    'src/zimg/graph/graphengine_except.h',
    'src/zimg/graph/simple_filters.cpp',
    'src/zimg/graph/simple_filters.h',

    'src/zimg/resize/filter.cpp',
    'src/zimg/resize/filter.h',
    'src/zimg/resize/resize.cpp',
    'src/zimg/resize/resize.h',
    'src/zimg/resize/resize_impl.cpp',
    'src/zimg/resize/resize_impl.h',

    'src/zimg/unresize/bilinear.cpp',
    'src/zimg/unresize/bilinear.h',
    'src/zimg/unresize/unresize.cpp',
    'src/zimg/unresize/unresize.h',
    'src/zimg/unresize/unresize_impl.cpp',
    'src/zimg/unresize/unresize_impl.h'
]

if opt_ARMSIMD
    sources += [
        'src/zimg/colorspace/arm/operation_impl_arm.cpp',
        'src/zimg/colorspace/arm/operation_impl_arm.h',
        'src/zimg/common/arm/cpuinfo_arm.cpp',
        'src/zimg/common/arm/cpuinfo_arm.h',
        'src/zimg/common/arm/neon_util.cpp',
        'src/zimg/common/arm/neon_util.h',
        'src/zimg/depth/arm/depth_convert_arm.cpp',
        'src/zimg/depth/arm/depth_convert_arm.h',
        'src/zimg/depth/arm/dither_arm.cpp',
        'src/zimg/depth/arm/dither_arm.h',
        'src/zimg/depth/arm/f16c_arm.h',
        'src/zimg/resize/arm/resize_impl_arm.cpp',
        'src/zimg/resize/arm/resize_impl_arm.h'
    ]

    NEON_CFLAGS = ['-march=armv7-a', '-mfpu=neon-vfpv4']
    if cxx.has_argument('-mfp16-format=ieee')
        NEON_CFLAGS += ['-mfp16-format=ieee']
    endif

    link_libs += static_library(
        'neon',
        [
            'src/zimg/colorspace/arm/operation_impl_neon.cpp',
            'src/zimg/depth/arm/depth_convert_neon.cpp',
            'src/zimg/depth/arm/dither_neon.cpp',
            'src/zimg/depth/arm/f16c_neon.cpp',
            'src/zimg/resize/arm/resize_impl_neon.cpp'
        ],
        c_args: compile_args + NEON_CFLAGS,
        cpp_args: compile_args + NEON_CFLAGS,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )
endif

if opt_X86SIMD
    sources += [
        'src/zimg/colorspace/x86/operation_impl_x86.cpp',
        'src/zimg/colorspace/x86/operation_impl_x86.h',

        'src/zimg/common/x86/avx_util.h',
        'src/zimg/common/x86/avx2_util.h',
        'src/zimg/common/x86/avx512_util.h',
        'src/zimg/common/x86/cpuinfo_x86.cpp',
        'src/zimg/common/x86/cpuinfo_x86.h',
        'src/zimg/common/x86/sse_util.h',
        'src/zimg/common/x86/sse2_util.h',
        'src/zimg/common/x86/x86util.cpp',
        'src/zimg/common/x86/x86util.h',

        'src/zimg/depth/x86/depth_convert_x86.cpp',
        'src/zimg/depth/x86/depth_convert_x86.h',
        'src/zimg/depth/x86/dither_x86.cpp',
        'src/zimg/depth/x86/dither_x86.h',
        'src/zimg/depth/x86/f16c_x86.h',

        'src/zimg/resize/x86/resize_impl_x86.cpp',
        'src/zimg/resize/x86/resize_impl_x86.h',
        'src/zimg/unresize/x86/unresize_impl_x86.cpp',
        'src/zimg/unresize/x86/unresize_impl_x86.h'
    ]

    link_libs += static_library(
        'sse',
        [
            'src/zimg/colorspace/x86/operation_impl_sse.cpp',
            'src/zimg/resize/x86/resize_impl_sse.cpp',
            'src/zimg/unresize/x86/unresize_impl_sse.cpp',
        ],
        c_args: compile_args + ['-msse'],
        cpp_args: compile_args + ['-msse'],
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )

    link_libs += static_library(
        'sse2',
        [
            'src/zimg/colorspace/x86/operation_impl_sse2.cpp',
            'src/zimg/depth/x86/depth_convert_sse2.cpp',
            'src/zimg/depth/x86/dither_sse2.cpp',
            'src/zimg/depth/x86/error_diffusion_sse2.cpp',
            'src/zimg/depth/x86/f16c_sse2.cpp',
            'src/zimg/resize/x86/resize_impl_sse2.cpp',
        ],
        c_args: compile_args + ['-msse2'],
        cpp_args: compile_args + ['-msse2'],
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )

    SNB_CFLAGS = []
    if cxx.has_argument('-mtune=sandybridge')
        SNB_CFLAGS += ['-mtune=sandybridge']
    endif

    link_libs += static_library(
        'avx',
        [
            'src/zimg/colorspace/x86/operation_impl_avx.cpp',
            'src/zimg/resize/x86/resize_impl_avx.cpp',
        ],
        c_args: compile_args + ['-mavx'] + SNB_CFLAGS,
        cpp_args: compile_args + ['-mavx'] + SNB_CFLAGS,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )

    IVB_CFLAGS = []
    if cxx.has_argument('-mtune=ivybridge')
        IVB_CFLAGS += ['-mtune=ivybridge']
    endif

    link_libs += static_library(
        'f16c',
        [
            'src/zimg/depth/x86/f16c_ivb.cpp',
        ],
        c_args: compile_args + ['-mavx', '-mf16c'] + IVB_CFLAGS,
        cpp_args: compile_args + ['-mavx', '-mf16c'] + IVB_CFLAGS,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )

    HSW_CFLAGS = []
    if cxx.has_argument('-mtune=haswell')
        HSW_CFLAGS += ['-mtune=haswell']
    endif

    link_libs += static_library(
        'avx2',
        [
            'src/zimg/colorspace/x86/operation_impl_avx2.cpp',
            'src/zimg/depth/x86/depth_convert_avx2.cpp',
            'src/zimg/depth/x86/dither_avx2.cpp',
            'src/zimg/depth/x86/error_diffusion_avx2.cpp',
            'src/zimg/resize/x86/resize_impl_avx2.cpp'
        ],
        c_args: compile_args + ['-mavx2', '-mf16c', '-mfma'] + HSW_CFLAGS,
        cpp_args: compile_args + ['-mavx2', '-mf16c', '-mfma'] + HSW_CFLAGS,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )
endif # X86SIMD

if opt_X86SIMD_AVX512
    avx512_args = ['-mavx512f', '-mavx512cd', '-mavx512vl', '-mavx512bw', '-mavx512dq']
    avx512vnni_args = ['-mavx512f', '-mavx512cd', '-mavx512vl', '-mavx512bw', '-mavx512dq', '-mavx512vnni']

    if cxx.has_argument('-mtune=skylake-avx512')
        avx512_args += ['-mtune=skylake-avx512']
    endif

    if cxx.has_argument('-mtune=cascadelake')
        avx512vnni_args += ['-mtune=cascadelake']
    endif

    link_libs += static_library(
        'avx512',
        [
            'src/zimg/colorspace/x86/gamma_constants_avx512.cpp',
            'src/zimg/colorspace/x86/gamma_constants_avx512.h',
            'src/zimg/colorspace/x86/operation_impl_avx512.cpp',
            'src/zimg/depth/x86/depth_convert_avx512.cpp',
            'src/zimg/depth/x86/dither_avx512.cpp',
            'src/zimg/resize/x86/resize_impl_avx512.cpp',
            'src/zimg/resize/x86/resize_impl_avx512_common.h',
        ],
        c_args: compile_args + avx512_args,
        cpp_args: compile_args + avx512_args,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )

    link_libs += static_library(
        'avx512vnni',
        [
            'src/zimg/resize/x86/resize_impl_avx512_vnni.cpp',
        ],
        c_args: compile_args + avx512vnni_args,
        cpp_args: compile_args + avx512vnni_args,
        gnu_symbol_visibility: 'hidden',
        include_directories: incl_dirs
    )
endif # X86SIMD_AVX512

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

deps = [
    vapoursynth_dep
]

if host_machine.system() == 'windows'
    deps += [meson.get_compiler('cpp').find_library('winpthread', static: true)]
else
    deps += [dependency('threads')]
endif

zimg = static_library('zimg', sources,
    c_args: compile_args,
    cpp_args: compile_args,
    dependencies : deps,
    link_with: link_libs,
    include_directories: incl_dirs,
)

zimg_patched_dep = declare_dependency(
    dependencies: deps,
    link_with: zimg,
    include_directories: incl_dirs,
)