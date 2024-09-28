/*
* Copyright (c) 2015-2020 Hoppsan G. Pig
* Copyright (c) 2024-2024 Setsugen no ao
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#define ZIMGXX_NAMESPACE vszimgxx
#include "zimg/api/zimg++.hpp"
#include "zimg/common/cpuinfo.h"
#include "zimg/common/static_map.h"
#include "zimg/graph/filtergraph.h"
#include "zimg/graph/graphbuilder.h"
#include "zimg/depth/depth.h"
#include "zimg/resize/filter.h"

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"

using namespace vsh;

using namespace zimg::colorspace;
using namespace zimg::resize;

using zimg::CPUClass;
using zimg::PixelType;

using zimg::graph::GraphBuilder;

using zimg::depth::DitherType;

namespace {


struct chromaloc_pair {
    zimg_chroma_location_e value;
    GraphBuilder::ChromaLocationW first;
    GraphBuilder::ChromaLocationH second;

    operator std::pair<GraphBuilder::ChromaLocationW, GraphBuilder::ChromaLocationH>() const {
        return as_pair();
    }

    std::pair<GraphBuilder::ChromaLocationW, GraphBuilder::ChromaLocationH> as_pair() const {
        return { first, second };
    }
}; 


using namespace std::string_literals;

const std::unordered_map<std::string, zimg_cpu_type_e> g_cpu_type_table{
    { "none",      ZIMG_CPU_NONE },
    { "auto",      ZIMG_CPU_AUTO },
    { "auto64",    ZIMG_CPU_AUTO_64B },
#if defined(ZIMG_X86)
    { "mmx",       ZIMG_CPU_X86_MMX },
    { "sse",       ZIMG_CPU_X86_SSE },
    { "sse2",      ZIMG_CPU_X86_SSE2 },
    { "sse3",      ZIMG_CPU_X86_SSE3 },
    { "ssse3",     ZIMG_CPU_X86_SSSE3 },
    { "sse41",     ZIMG_CPU_X86_SSE41 },
    { "sse42",     ZIMG_CPU_X86_SSE42 },
    { "avx",       ZIMG_CPU_X86_AVX },
    { "f16c",      ZIMG_CPU_X86_F16C },
    { "avx2",      ZIMG_CPU_X86_AVX2 },
    { "avx512f",   ZIMG_CPU_X86_AVX512F },
    { "avx512skx", ZIMG_CPU_X86_AVX512_SKX },
    { "avx512clx", ZIMG_CPU_X86_AVX512_CLX },
    { "avx512snc", ZIMG_CPU_X86_AVX512_SNC },
#endif
};



const std::unordered_map<zimg_cpu_type_e, CPUClass> h_cpu_type_table{
    { ZIMG_CPU_NONE,           CPUClass::NONE },
    { ZIMG_CPU_AUTO,           CPUClass::AUTO },
    { ZIMG_CPU_AUTO_64B,       CPUClass::AUTO_64B },
#if defined(ZIMG_X86)
    { ZIMG_CPU_X86_MMX,        CPUClass::NONE },
    { ZIMG_CPU_X86_SSE,        CPUClass::X86_SSE },
    { ZIMG_CPU_X86_SSE2,       CPUClass::X86_SSE2 },
    { ZIMG_CPU_X86_SSE3,       CPUClass::X86_SSE2 },
    { ZIMG_CPU_X86_SSSE3,      CPUClass::X86_SSE2 },
    { ZIMG_CPU_X86_SSE41,      CPUClass::X86_SSE2 },
    { ZIMG_CPU_X86_SSE42,      CPUClass::X86_SSE2 },
    { ZIMG_CPU_X86_AVX,        CPUClass::X86_AVX },
    { ZIMG_CPU_X86_F16C,       CPUClass::X86_F16C },
    { ZIMG_CPU_X86_AVX2,       CPUClass::X86_AVX2 },
    { ZIMG_CPU_X86_AVX512F,    CPUClass::X86_AVX2 },
    { ZIMG_CPU_X86_AVX512_SKX, CPUClass::X86_AVX512 },
    { ZIMG_CPU_X86_AVX512_CLX, CPUClass::X86_AVX512_CLX },
    { ZIMG_CPU_X86_AVX512_PMC, CPUClass::X86_AVX512 },
    { ZIMG_CPU_X86_AVX512_SNC, CPUClass::X86_AVX512_CLX },
#endif
};


const std::unordered_map<std::string, zimg_pixel_range_e> g_range_table{
    { "limited", ZIMG_RANGE_LIMITED },
    { "full",    ZIMG_RANGE_FULL },
};


const std::unordered_map<zimg_pixel_range_e, bool> h_range_table{
    { ZIMG_RANGE_LIMITED, false },
    { ZIMG_RANGE_FULL,    true },
};


const std::unordered_map<std::string, zimg_chroma_location_e> g_chromaloc_table{
    { "left",        ZIMG_CHROMA_LEFT },
    { "center",      ZIMG_CHROMA_CENTER },
    { "top_left",    ZIMG_CHROMA_TOP_LEFT },
    { "top",         ZIMG_CHROMA_TOP },
    { "bottom_left", ZIMG_CHROMA_BOTTOM_LEFT },
    { "bottom",      ZIMG_CHROMA_BOTTOM },
};


const std::unordered_map<zimg_chroma_location_e, chromaloc_pair> h_chromaloc_table{
#define EXPAND_CHROMALOC(value, first, second) { value, { value, first, second } }
    EXPAND_CHROMALOC(ZIMG_CHROMA_LEFT,        GraphBuilder::ChromaLocationW::LEFT,   GraphBuilder::ChromaLocationH::CENTER),
    EXPAND_CHROMALOC(ZIMG_CHROMA_CENTER,      GraphBuilder::ChromaLocationW::CENTER, GraphBuilder::ChromaLocationH::CENTER),
    EXPAND_CHROMALOC(ZIMG_CHROMA_TOP_LEFT,    GraphBuilder::ChromaLocationW::LEFT,   GraphBuilder::ChromaLocationH::TOP),
    EXPAND_CHROMALOC(ZIMG_CHROMA_TOP,         GraphBuilder::ChromaLocationW::CENTER, GraphBuilder::ChromaLocationH::TOP),
    EXPAND_CHROMALOC(ZIMG_CHROMA_BOTTOM_LEFT, GraphBuilder::ChromaLocationW::LEFT,   GraphBuilder::ChromaLocationH::BOTTOM),
    EXPAND_CHROMALOC(ZIMG_CHROMA_BOTTOM,      GraphBuilder::ChromaLocationW::CENTER, GraphBuilder::ChromaLocationH::BOTTOM),
#undef EXPAND_CHROMALOC
};


const std::unordered_map<std::string, zimg_matrix_coefficients_e> g_matrix_table{
    { "rgb",         ZIMG_MATRIX_RGB },
    { "709",         ZIMG_MATRIX_BT709 },
    { "unspec",      ZIMG_MATRIX_UNSPECIFIED },
    { "170m",        ZIMG_MATRIX_ST170_M },
    { "240m",        ZIMG_MATRIX_ST240_M },
    { "470bg",       ZIMG_MATRIX_BT470_BG },
    { "fcc",         ZIMG_MATRIX_FCC },
    { "ycgco",       ZIMG_MATRIX_YCGCO },
    { "2020ncl",     ZIMG_MATRIX_BT2020_NCL },
    { "2020cl",      ZIMG_MATRIX_BT2020_CL },
    { "chromacl",    ZIMG_MATRIX_CHROMATICITY_DERIVED_CL },
    { "chromancl",   ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL },
    { "ictcp",       ZIMG_MATRIX_ICTCP },
};


const std::unordered_map<zimg_matrix_coefficients_e, MatrixCoefficients> h_matrix_table{
    { ZIMG_MATRIX_RGB,                      MatrixCoefficients::RGB },
    { ZIMG_MATRIX_BT709,                    MatrixCoefficients::REC_709 },
    { ZIMG_MATRIX_UNSPECIFIED,              MatrixCoefficients::UNSPECIFIED },
    { ZIMG_MATRIX_FCC,                      MatrixCoefficients::FCC },
    { ZIMG_MATRIX_BT470_BG,                 MatrixCoefficients::REC_601 },
    { ZIMG_MATRIX_ST170_M,                  MatrixCoefficients::REC_601 },
    { ZIMG_MATRIX_ST240_M,                  MatrixCoefficients::SMPTE_240M },
    { ZIMG_MATRIX_YCGCO,                    MatrixCoefficients::YCGCO },
    { ZIMG_MATRIX_BT2020_NCL,               MatrixCoefficients::REC_2020_NCL },
    { ZIMG_MATRIX_BT2020_CL,                MatrixCoefficients::REC_2020_CL },
    { ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL, MatrixCoefficients::CHROMATICITY_DERIVED_NCL },
    { ZIMG_MATRIX_CHROMATICITY_DERIVED_CL,  MatrixCoefficients::CHROMATICITY_DERIVED_CL },
    { ZIMG_MATRIX_ICTCP,                    MatrixCoefficients::REC_2100_ICTCP },
};


const std::unordered_map<std::string, zimg_transfer_characteristics_e> g_transfer_table{
    { "709",     ZIMG_TRANSFER_BT709 },
    { "unspec",  ZIMG_TRANSFER_UNSPECIFIED },
    { "601",     ZIMG_TRANSFER_BT601 },
    { "linear",  ZIMG_TRANSFER_LINEAR },
    { "2020_10", ZIMG_TRANSFER_BT2020_10 },
    { "2020_12", ZIMG_TRANSFER_BT2020_12 },
    { "240m",    ZIMG_TRANSFER_ST240_M },
    { "470m",    ZIMG_TRANSFER_BT470_M },
    { "470bg",   ZIMG_TRANSFER_BT470_BG },
    { "log100",  ZIMG_TRANSFER_LOG_100 },
    { "log316",  ZIMG_TRANSFER_LOG_316 },
    { "st2084",  ZIMG_TRANSFER_ST2084 },
    { "std-b67", ZIMG_TRANSFER_ARIB_B67 },
    { "st428",   ZIMG_TRANSFER_ST428 },
    { "srgb",    ZIMG_TRANSFER_IEC_61966_2_1 },
    { "xvycc",   ZIMG_TRANSFER_IEC_61966_2_4 },
};


const std::unordered_map<zimg_transfer_characteristics_e, TransferCharacteristics> h_transfer_table{
    { ZIMG_TRANSFER_BT709,         TransferCharacteristics::REC_709 },
    { ZIMG_TRANSFER_UNSPECIFIED,   TransferCharacteristics::UNSPECIFIED },
    { ZIMG_TRANSFER_ST240_M,       TransferCharacteristics::SMPTE_240M },
    { ZIMG_TRANSFER_BT601,         TransferCharacteristics::REC_709 },
    { ZIMG_TRANSFER_BT470_M,       TransferCharacteristics::REC_470_M },
    { ZIMG_TRANSFER_BT470_BG,      TransferCharacteristics::REC_470_BG },
    { ZIMG_TRANSFER_IEC_61966_2_4, TransferCharacteristics::XVYCC },
    { ZIMG_TRANSFER_IEC_61966_2_1, TransferCharacteristics::SRGB },
    { ZIMG_TRANSFER_BT2020_10,     TransferCharacteristics::REC_709 },
    { ZIMG_TRANSFER_BT2020_12,     TransferCharacteristics::REC_709 },
    { ZIMG_TRANSFER_LINEAR,        TransferCharacteristics::LINEAR },
    { ZIMG_TRANSFER_LOG_100,       TransferCharacteristics::LOG_100 },
    { ZIMG_TRANSFER_LOG_316,       TransferCharacteristics::LOG_316 },
    { ZIMG_TRANSFER_ST2084,        TransferCharacteristics::ST_2084 },
    { ZIMG_TRANSFER_ST428,         TransferCharacteristics::ST_428 },
    { ZIMG_TRANSFER_ARIB_B67,      TransferCharacteristics::ARIB_B67 },
};


const std::unordered_map<std::string, zimg_color_primaries_e> g_primaries_table{
    { "709",       ZIMG_PRIMARIES_BT709 },
    { "unspec",    ZIMG_PRIMARIES_UNSPECIFIED },
    { "170m",      ZIMG_PRIMARIES_ST170_M },
    { "240m",      ZIMG_PRIMARIES_ST240_M },
    { "470m",      ZIMG_PRIMARIES_BT470_M },
    { "470bg",     ZIMG_PRIMARIES_BT470_BG },
    { "film",      ZIMG_PRIMARIES_FILM },
    { "2020",      ZIMG_PRIMARIES_BT2020 },
    { "st428",     ZIMG_PRIMARIES_ST428 },
    { "xyz",       ZIMG_PRIMARIES_ST428 },
    { "st431-2",   ZIMG_PRIMARIES_ST431_2 },
    { "st432-1",   ZIMG_PRIMARIES_ST432_1 },
    { "ebu3213-e", ZIMG_PRIMARIES_EBU3213_E },
};


const std::unordered_map<zimg_color_primaries_e, ColorPrimaries> h_primaries_table{
    { ZIMG_PRIMARIES_BT470_M,     ColorPrimaries::REC_470_M },
    { ZIMG_PRIMARIES_BT470_BG,    ColorPrimaries::REC_470_BG },
    { ZIMG_PRIMARIES_BT709,       ColorPrimaries::REC_709 },
    { ZIMG_PRIMARIES_UNSPECIFIED, ColorPrimaries::UNSPECIFIED },
    { ZIMG_PRIMARIES_ST170_M,     ColorPrimaries::SMPTE_C },
    { ZIMG_PRIMARIES_ST240_M,     ColorPrimaries::SMPTE_C },
    { ZIMG_PRIMARIES_FILM,        ColorPrimaries::FILM },
    { ZIMG_PRIMARIES_BT2020,      ColorPrimaries::REC_2020 },
    { ZIMG_PRIMARIES_ST428,       ColorPrimaries::XYZ },
    { ZIMG_PRIMARIES_ST431_2,     ColorPrimaries::DCI_P3 },
    { ZIMG_PRIMARIES_ST432_1,     ColorPrimaries::DCI_P3_D65 },
    { ZIMG_PRIMARIES_EBU3213_E,   ColorPrimaries::EBU_3213_E },
};


const std::unordered_map<std::string, zimg_dither_type_e> g_dither_type_table{
    { "none",            ZIMG_DITHER_NONE },
    { "ordered",         ZIMG_DITHER_ORDERED },
    { "random",          ZIMG_DITHER_RANDOM },
    { "error_diffusion", ZIMG_DITHER_ERROR_DIFFUSION },
};


const std::unordered_map<zimg_dither_type_e, DitherType> h_dither_type_table{
    { ZIMG_DITHER_NONE,            DitherType::NONE },
    { ZIMG_DITHER_ORDERED,         DitherType::ORDERED },
    { ZIMG_DITHER_RANDOM,          DitherType::RANDOM },
    { ZIMG_DITHER_ERROR_DIFFUSION, DitherType::ERROR_DIFFUSION },
};


const std::unordered_map<std::string, zimg_resample_filter_e> g_resample_filter_table{
    { "point",    ZIMG_RESIZE_POINT },
    { "bilinear", ZIMG_RESIZE_BILINEAR },
    { "bicubic",  ZIMG_RESIZE_BICUBIC },
    { "spline16", ZIMG_RESIZE_SPLINE16 },
    { "spline36", ZIMG_RESIZE_SPLINE36 },
    { "spline64", ZIMG_RESIZE_SPLINE64 },
    { "lanczos",  ZIMG_RESIZE_LANCZOS },
};


std::unique_ptr<Filter> translate_resize_filter(zimg_resample_filter_e filter_type, double param_a, double param_b) {
    try {
        switch (filter_type) {
        case ZIMG_RESIZE_POINT:
            return std::make_unique<PointFilter>();
        case ZIMG_RESIZE_BILINEAR:
            return std::make_unique<BilinearFilter>();
        case ZIMG_RESIZE_BICUBIC:
            param_a = std::isnan(param_a) ? BicubicFilter::DEFAULT_B : param_a;
            param_b = std::isnan(param_b) ? BicubicFilter::DEFAULT_C : param_b;
            return std::make_unique<BicubicFilter>(param_a, param_b);
        case ZIMG_RESIZE_SPLINE16:
            return std::make_unique<Spline16Filter>();
        case ZIMG_RESIZE_SPLINE36:
            return std::make_unique<Spline36Filter>();
        case ZIMG_RESIZE_SPLINE64:
            return std::make_unique<Spline64Filter>();
        case ZIMG_RESIZE_LANCZOS:
            param_a = std::isnan(param_a) ? LanczosFilter::DEFAULT_TAPS : std::max(param_a, 1.0);
            return std::make_unique<LanczosFilter>(static_cast<unsigned>(param_a));
        default:
            zimg::error::throw_<zimg::error::EnumOutOfRange>("unrecognized resampling filter");
        }
    } catch (const std::bad_alloc &) {
        zimg::error::throw_<zimg::error::OutOfMemory>();
    }
}


struct vsrz_image_format {
    unsigned version;

    unsigned width;
    unsigned height;
    PixelType pixel_type;

    unsigned subsample_w;
    unsigned subsample_h;

    GraphBuilder::ColorFamily color_family;
    zimg_matrix_coefficients_e matrix_coefficients;
    zimg_transfer_characteristics_e transfer_characteristics;
    zimg_color_primaries_e color_primaries;

    unsigned depth;
    bool fullrange;

    GraphBuilder::FieldParity field_parity;
    chromaloc_pair chroma_location;

    struct {
        double left;
        double top;
        double width;
        double height;
    } active_region;

    GraphBuilder::AlphaType alpha;               
    
    vsrz_image_format() {
        this->version = ZIMG_API_VERSION;

        this->width = 0;
        this->height = 0;
        this->pixel_type = static_cast<PixelType>(-1);

        this->subsample_w = 0;
        this->subsample_h = 0;

        this->color_family = GraphBuilder::ColorFamily::GREY;
        this->matrix_coefficients = zimg_matrix_coefficients_e::ZIMG_MATRIX_UNSPECIFIED;
        this->transfer_characteristics = zimg_transfer_characteristics_e::ZIMG_TRANSFER_UNSPECIFIED;
        this->color_primaries = zimg_color_primaries_e::ZIMG_PRIMARIES_UNSPECIFIED;

        this->depth = 0;
        this->fullrange = false;

        this->field_parity = GraphBuilder::FieldParity::PROGRESSIVE;
        this->chroma_location = h_chromaloc_table.find(ZIMG_CHROMA_LEFT)->second;

        this->active_region.left = NAN;
        this->active_region.top = NAN;
        this->active_region.width = NAN;
        this->active_region.height = NAN;

        this->alpha = GraphBuilder::AlphaType::NONE;
    }
};

typedef struct filter_graph_builder_params {
    DitherType dither_type;
    CPUClass cpu_type;
    GraphBuilder::force_state force;

    double nominal_peak_luminance;

    filter_graph_builder_params() {
        dither_type = DitherType::NONE;
        cpu_type = CPUClass::AUTO;
    }
} filter_graph_builder_params;


template <class T, class U>
T range_check_integer(U x, const char *key) {
    if (x < std::numeric_limits<T>::min() || x > std::numeric_limits<T>::max())
        throw std::range_error{ "value for key \""s + key + "\" out of range" };
    return static_cast<T>(x);
}

template <class T>
T propGetScalar(const VSMap *map, const char *key, const VSAPI *vsapi);

template <>
int propGetScalar<int>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    auto x = vsapi->mapGetInt(map, key, 0, nullptr);
    return range_check_integer<int>(x, key);
}

template <>
unsigned propGetScalar<unsigned>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    auto x = vsapi->mapGetInt(map, key, 0, nullptr);
    return range_check_integer<unsigned>(x, key);
}

template <>
double propGetScalar<double>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    return vsapi->mapGetFloat(map, key, 0, nullptr);
}

template <>
const char *propGetScalar<const char *>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    return vsapi->mapGetData(map, key, 0, nullptr);
}

template <class T>
T propGetScalarDef(const VSMap *map, const char *key, T def, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(map, key) > 0)
        return propGetScalar<T>(map, key, vsapi);
    else
        return def;
}

template <class T, class U, class S, class Pred>
void propGetIfValid(const VSMap *map, const char *key, S *out, const std::unordered_map<S, U> &enum_table, Pred pred, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(map, key) > 0) {
        T x = propGetScalar<T>(map, key, vsapi);
        if (pred(x)) {
            auto it = enum_table.find(static_cast<S>(x));
            if (it != enum_table.end())
                *out = it->first;
            else
                throw std::runtime_error{ "bad value: "s + key };
        }
    }
}

template <class T, class U, class S, class Pred>
void propGetIfValid(const VSMap *map, const char *key, U *out, const std::unordered_map<S, U> &enum_table, Pred pred, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(map, key) > 0) {
        T x = propGetScalar<T>(map, key, vsapi);
        if (pred(x)) {
            auto it = enum_table.find(static_cast<S>(x));
            if (it != enum_table.end())
                *out = it->second;
            else
                throw std::runtime_error{ "bad value: "s + key };
        }
    }
}


template <class Map, class Key>
typename Map::mapped_type search_enum_map(const Map &map, const Key &key, const char *msg)
{
    auto it = map.find(key);
    if (it == map.end())
        zimg::error::throw_<zimg::error::EnumOutOfRange>(msg);
    return it->second;
}


void translate_vs_pixel_type(const VSVideoFormat *format, PixelType *out, const VSAPI *vsapi) {
    if (format->sampleType == stInteger && format->bytesPerSample == 1)
        *out = PixelType::BYTE;
    else if (format->sampleType == stInteger && format->bytesPerSample == 2)
        *out = PixelType::WORD;
    else if (format->sampleType == stFloat && format->bytesPerSample == 2)
        *out = PixelType::HALF;
    else if (format->sampleType == stFloat && format->bytesPerSample == 4)
        *out = PixelType::FLOAT;
    else {
        char buffer[32];
        vsapi->getVideoFormatName(format, buffer);
        throw std::runtime_error{ "no matching pixel type for format: "s + buffer };
    }
}

void translate_vs_color_family(VSColorFamily cf, GraphBuilder::ColorFamily *out, zimg_matrix_coefficients_e *out_matrix) {
    switch (cf) {
    case cfGray:
        *out = GraphBuilder::ColorFamily::GREY;
        *out_matrix = zimg_matrix_coefficients_e::ZIMG_MATRIX_UNSPECIFIED;
        break;
    case cfRGB:
        *out = GraphBuilder::ColorFamily::RGB;
        *out_matrix = zimg_matrix_coefficients_e::ZIMG_MATRIX_RGB;
        break;
    case cfYUV:
        *out = GraphBuilder::ColorFamily::YUV;
        *out_matrix = zimg_matrix_coefficients_e::ZIMG_MATRIX_UNSPECIFIED;
        break;
    default:
        throw std::runtime_error{ "unsupported color family" };
    }
}

void translate_vsformat(const VSVideoFormat *vsformat, vsrz_image_format *format, const VSAPI *vsapi) {
    translate_vs_color_family(static_cast<VSColorFamily>(vsformat->colorFamily), &format->color_family, &format->matrix_coefficients);
    translate_vs_pixel_type(vsformat, &format->pixel_type, vsapi);
    format->depth = vsformat->bitsPerSample;

    format->subsample_w = vsformat->subSamplingW;
    format->subsample_h = vsformat->subSamplingH;
    format->fullrange = (format->color_family == GraphBuilder::ColorFamily::RGB);

    format->field_parity = GraphBuilder::FieldParity::PROGRESSIVE;
    format->chroma_location = h_chromaloc_table.find((format->subsample_w || format->subsample_h) ? ZIMG_CHROMA_LEFT : ZIMG_CHROMA_CENTER)->second;
}


void import_graph_state_common(const vsrz_image_format &src, GraphBuilder::state *out, const filter_graph_builder_params &params)
{
    out->width = src.width;
    out->height = src.height;
    out->type = src.pixel_type;
    out->subsample_w = src.subsample_w;
    out->subsample_h = src.subsample_h;

    out->color = src.color_family;

    out->depth = src.depth ? src.depth : zimg::pixel_depth(out->type);
    out->fullrange = src.fullrange;

    out->parity = src.field_parity;
    std::tie(out->chroma_location_w, out->chroma_location_h) = src.chroma_location.as_pair();

    out->active_left = std::isnan(src.active_region.left) ? 0 : src.active_region.left;
    out->active_top = std::isnan(src.active_region.top) ? 0 : src.active_region.top;
    out->active_width = std::isnan(src.active_region.width) ? src.width : src.active_region.width;
    out->active_height = std::isnan(src.active_region.height) ? src.height : src.active_region.height;

    out->force = params.force;
}


void import_frame_props(const VSMap *props, vsrz_image_format *format, bool *interlaced, const VSAPI *vsapi) {
    propGetIfValid<int>(props, "_ChromaLocation", &format->chroma_location, h_chromaloc_table, [](int x) { return x >= 0; }, vsapi);

    if (vsapi->mapNumElements(props, "_ColorRange") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_ColorRange", 0, nullptr);

        switch (x) {
        case 0:
        case 1:
            format->fullrange = !x;
            break;
        default:
            throw std::runtime_error{ "bad _ColorRange value: " + std::to_string(x) };
        }
    }

    // Ignore UNSPECIFIED values from properties, since the user can specify them.
    propGetIfValid<int>(props, "_Matrix", &format->matrix_coefficients, h_matrix_table, [](int x) { return x != ZIMG_MATRIX_UNSPECIFIED; }, vsapi);
    propGetIfValid<int>(props, "_Transfer", &format->transfer_characteristics, h_transfer_table, [](int x) { return x != ZIMG_TRANSFER_UNSPECIFIED; }, vsapi);
    propGetIfValid<int>(props, "_Primaries", &format->color_primaries, h_primaries_table, [](int x) { return x != ZIMG_PRIMARIES_UNSPECIFIED; }, vsapi);

    bool is_interlaced = false;
    if (vsapi->mapNumElements(props, "_Field") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_Field", 0, nullptr);

        if (x == 0)
            format->field_parity = GraphBuilder::FieldParity::BOTTOM;
        else if (x == 1)
            format->field_parity = GraphBuilder::FieldParity::TOP;
        else
            throw std::runtime_error{ "bad _Field value: " + std::to_string(x) };
    } else if (vsapi->mapNumElements(props, "_FieldBased") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_FieldBased", 0, nullptr);

        if (x != VSC_FIELD_PROGRESSIVE && x != VSC_FIELD_BOTTOM && x != VSC_FIELD_TOP)
            throw std::runtime_error{ "bad _FieldBased value: " + std::to_string(x) };

        is_interlaced = x == 1 || x == 2;
    }

    if (is_interlaced) {
        format->active_region.top /= 2;
        format->active_region.height /= 2;
    }

    *interlaced = is_interlaced;
}


template <class T, class S>
int get_from_table(T value, const std::unordered_map<S, T> &enum_table) {
    auto it = std::find_if(
        std::begin(enum_table), std::end(enum_table),
        [&value](auto && pair) { return pair.second == value; }
    );

    if (it == std::end(enum_table))
        return -1;

    return static_cast<int>(it->first);
};

void export_frame_props(const vsrz_image_format &format, VSMap *props, const VSAPI *vsapi) {
    auto set_int_if_positive = [&](const char *key, int x) {
        if (x >= 0)
            vsapi->mapSetInt(props, key, x, maReplace);
        else
            vsapi->mapDeleteKey(props, key);
    };

    if (format.color_family == GraphBuilder::ColorFamily::YUV && (format.subsample_w || format.subsample_h))
        vsapi->mapSetInt(props, "_ChromaLocation", format.chroma_location.value, maReplace);
    else
        vsapi->mapDeleteKey(props, "_ChromaLocation");

    vsapi->mapSetInt(props, "_ColorRange", (int) !format.fullrange, maReplace);

    set_int_if_positive("_Matrix", (int) format.matrix_coefficients);
    set_int_if_positive("_Transfer", (int) format.transfer_characteristics);
    set_int_if_positive("_Primaries", (int) format.color_primaries);
}

void propagate_sar(const VSMap *src_props, VSMap *dst_props, const vsrz_image_format &src_format, const vsrz_image_format &dst_format, const VSAPI *vsapi) {
    int64_t sar_num = 0;
    int64_t sar_den = 0;

    if (vsapi->mapNumElements(src_props, "_SARNum") > 0)
        sar_num = vsapi->mapGetInt(src_props, "_SARNum", 0, nullptr);
    if (vsapi->mapNumElements(dst_props, "_SARDen") > 0)
        sar_den = vsapi->mapGetInt(dst_props, "_SARDen", 0, nullptr);

    if (sar_num <= 0 || sar_den <= 0) {
        vsapi->mapDeleteKey(dst_props, "_SARNum");
        vsapi->mapDeleteKey(dst_props, "_SARDen");
    } else {
        if (!std::isnan(src_format.active_region.width) && src_format.active_region.width != src_format.width)
            muldivRational(&sar_num, &sar_den, std::llround(src_format.active_region.width * 16), static_cast<int64_t>(dst_format.width) * 16);
        else
            muldivRational(&sar_num, &sar_den, src_format.width, dst_format.width);

        if (!std::isnan(src_format.active_region.height) && src_format.active_region.height != src_format.height)
            muldivRational(&sar_num, &sar_den, static_cast<int64_t>(dst_format.height) * 16, std::llround(src_format.active_region.height * 16));
        else
            muldivRational(&sar_num, &sar_den, dst_format.height, src_format.height);

        vsapi->mapSetInt(dst_props, "_SARNum", sar_num, maReplace);
        vsapi->mapSetInt(dst_props, "_SARDen", sar_den, maReplace);
    }
}


vszimgxx::zimage_buffer import_frame_as_buffer(VSFrame *frame, const VSAPI *vsapi) {
    vszimgxx::zimage_buffer buffer;
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(frame);
    for (unsigned p = 0; p < static_cast<unsigned>(format->numPlanes); ++p) {
        buffer.plane[p].data = vsapi->getWritePtr(frame, p);
        buffer.plane[p].stride = vsapi->getStride(frame, p);
        buffer.plane[p].mask = ZIMG_BUFFER_MAX;
    }
    return buffer;
}

vszimgxx::zimage_buffer_const import_frame_as_buffer_const(const VSFrame *frame, const VSAPI *vsapi) {
    vszimgxx::zimage_buffer_const buffer;
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(frame);
    for (unsigned p = 0; p < static_cast<unsigned>(format->numPlanes); ++p) {
        buffer.plane[p].data = vsapi->getReadPtr(frame, p);
        buffer.plane[p].stride = vsapi->getStride(frame, p);
        buffer.plane[p].mask = ZIMG_BUFFER_MAX;
    }
    return buffer;
}

template <class T>
T get_field_buffer(const T &buffer, unsigned num_planes, GraphBuilder::FieldParity parity) {
    T field = buffer;
    unsigned phase = parity == GraphBuilder::FieldParity::BOTTOM ? 1 : 0;

    for (unsigned p = 0; p < num_planes; ++p) {
        field.data(p) = field.line_at(phase, p);
        field.stride(p) *= 2;
    }
    return field;
}


bool operator==(const vsrz_image_format &a, const vsrz_image_format &b) {
    bool ret = true;

    ret = ret && a.width == b.width;
    ret = ret && a.height == b.height;
    ret = ret && a.pixel_type == b.pixel_type;
    ret = ret && a.subsample_w == b.subsample_w;
    ret = ret && a.subsample_h == b.subsample_h;
    ret = ret && a.color_family == b.color_family;

    if (a.color_family != GraphBuilder::ColorFamily::GREY)
        ret = ret && a.matrix_coefficients == b.matrix_coefficients;

    ret = ret && a.transfer_characteristics == b.transfer_characteristics;
    ret = ret && a.color_primaries == b.color_primaries;

    ret = ret && a.depth == b.depth;
    ret = ret && a.fullrange == b.fullrange;
    ret = ret && a.field_parity == b.field_parity;

    if (a.color_family == GraphBuilder::ColorFamily::YUV && (a.subsample_w || a.subsample_h))
        ret = ret && a.chroma_location.value == b.chroma_location.value;

    return ret;
}

bool operator!=(const vsrz_image_format &a, const vsrz_image_format &b) {
    return !operator==(a, b);
}

bool is_shifted(const vsrz_image_format &fmt) {
    bool ret = false;
    ret = ret || (!std::isnan(fmt.active_region.left) && fmt.active_region.left != 0);
    ret = ret || (!std::isnan(fmt.active_region.top) && fmt.active_region.top != 0);
    ret = ret || (!std::isnan(fmt.active_region.width) && fmt.active_region.width != fmt.width);
    ret = ret || (!std::isnan(fmt.active_region.height) && fmt.active_region.height != fmt.height);
    return ret;
}


enum class FieldOp {
    NONE,
    DEINTERLACE,
};


template <class S, class T>
static T lookup_enum_map(const S value, const std::unordered_map<S, T> &enum_table) {
    auto it = enum_table.find(value);
    if (it != enum_table.end())
        return it->second;

    throw std::runtime_error{ "bad value: "s + std::to_string((int) value) };
}


std::pair<GraphBuilder::state, GraphBuilder::state> import_graph_state(const vsrz_image_format &src, const vsrz_image_format &dst, const filter_graph_builder_params &params)
{
    GraphBuilder::state src_state{};
    GraphBuilder::state dst_state{};

    import_graph_state_common(src, &src_state, params);
    import_graph_state_common(dst, &dst_state, params);

    if (src.color_family == dst.color_family &&
        src.matrix_coefficients == dst.matrix_coefficients &&
        src.transfer_characteristics == dst.transfer_characteristics &&
        src.color_primaries == dst.color_primaries)
    {
        src_state.colorspace = ColorspaceDefinition{};
        dst_state.colorspace = ColorspaceDefinition{};
    } else {
        src_state.colorspace.matrix = lookup_enum_map(src.matrix_coefficients, h_matrix_table);
        src_state.colorspace.transfer = lookup_enum_map(src.transfer_characteristics, h_transfer_table);
        src_state.colorspace.primaries = lookup_enum_map(src.color_primaries, h_primaries_table);

        dst_state.colorspace.matrix = lookup_enum_map(dst.matrix_coefficients, h_matrix_table);
        dst_state.colorspace.transfer = lookup_enum_map(dst.transfer_characteristics, h_transfer_table);
        dst_state.colorspace.primaries = lookup_enum_map(dst.color_primaries, h_primaries_table);
    }

    return{ src_state, dst_state };
}


class CustomZimgFilter : public Filter {
    unsigned taps;
    VSFunction *func;

    const VSAPI *vsapi;

    mutable std::unordered_map<unsigned long long, double> cache;
    mutable std::shared_mutex cache_mutex;

public:
    CustomZimgFilter(unsigned taps, VSFunction *func, const VSAPI *vsapi) : taps(taps), func(func), vsapi(vsapi), cache{} {}

    ~CustomZimgFilter() {
        vsapi->freeFunction(func);
    }

    unsigned support() const override { return taps; };

    double operator()(double x) const override {
        {
            std::shared_lock<std::shared_mutex> lock(cache_mutex);
            auto it = cache.find((unsigned long long&)x);

            if (it != cache.end())
                return it->second;
        }

        VSMap *_map = vsapi->createMap();
        int _err;

        vsapi->mapSetFloat(_map, "x", x, maReplace);
        vsapi->callFunction(func, _map, _map);

        const char *ret_str = vsapi->mapGetError(_map);

        if (ret_str)
            throw zimg::error::Exception{ "There was an error running the custom kernel: " + std::string(ret_str) };

        double value = vsapi->mapGetFloat(_map, "val", 0, &_err);
        vsapi->clearMap(_map);

        if (_err)
            throw zimg::error::Exception{
                "Running custom_kernel(" + std::to_string(x) + ") returned error(" + std::to_string(_err) + ") for invalid value: " + std::to_string(value)
            };

        {
            std::lock_guard<std::shared_mutex> lock(cache_mutex);
            cache[(unsigned long long&)x] = value;
        }

        return value;
    };
};


struct vszimg_userdata {
    zimg_resample_filter_e filter;
    bool custom;
    FieldOp op;

    explicit vszimg_userdata(void *encoded) :
        filter{ static_cast<zimg_resample_filter_e>(reinterpret_cast<intptr_t>(encoded) & 0x3FFF) },
        custom{ static_cast<bool>((reinterpret_cast<intptr_t>(encoded) >> 14) & 0x1) },
        op{ static_cast<FieldOp>(reinterpret_cast<intptr_t>(encoded) >> 15) }
    {}

    vszimg_userdata(zimg_resample_filter_e filter, bool custom = false, FieldOp op = FieldOp::NONE) : filter{ filter }, custom { custom }, op{ op } {}

    void *encode() const { return reinterpret_cast<void *>((static_cast<intptr_t>(filter) & 0x3FFF) | (static_cast<intptr_t>(custom) << 14) | (static_cast<intptr_t>(op) << 15)); }

    operator void *() const { return encode(); }
};


class vszimg {
    struct frame_params {
        std::optional<zimg_matrix_coefficients_e> matrix;
        std::optional<zimg_transfer_characteristics_e> transfer;
        std::optional<zimg_color_primaries_e> primaries;
        std::optional<bool> fullrange;
        std::optional<chromaloc_pair> chromaloc;
    };

    struct graph_data {
        vszimgxx::FilterGraph graph;
        vsrz_image_format src_format;
        vsrz_image_format dst_format;

        graph_data(const vsrz_image_format &src_format, const vsrz_image_format &dst_format, const filter_graph_builder_params &params, std::unique_ptr<Filter> filters[2]) :
            src_format(src_format),
            dst_format(dst_format)
        {
            zimg_filter_graph *_graph;

            GraphBuilder::state src_state;
            GraphBuilder::state dst_state;
            GraphBuilder::params graph_params;
            GraphBuilder builder;

            std::tie(src_state, dst_state) = import_graph_state(src_format, dst_format, params);

            graph_params.filter = filters[0].get();
            graph_params.filter_uv = filters[1].get();
            graph_params.unresize = false;
            graph_params.dither_type = params.dither_type;
            graph_params.cpu = params.cpu_type;
            graph_params.peak_luminance = params.nominal_peak_luminance;
            graph_params.approximate_gamma = true;

            _graph = builder.set_source(src_state).connect(dst_state, &graph_params).build_graph().release();

            graph = vszimgxx::FilterGraph(_graph);
        }
    };

    std::shared_ptr<graph_data> m_graph_data_p;
    std::shared_ptr<graph_data> m_graph_data_t;
    std::shared_ptr<graph_data> m_graph_data_b;

    VSNode *m_node = nullptr;
    VSFunction *m_custom_kernel = nullptr;
    VSVideoInfo m_vi{};

    filter_graph_builder_params m_params;
    std::unique_ptr<Filter> filters[2];
    double m_src_left = NAN, m_src_top = NAN, m_src_width = NAN, m_src_height = NAN; // Propagated to zimage_format.

    frame_params m_frame_params;
    frame_params m_frame_params_in;

    FieldOp m_field_op = FieldOp::NONE;

    template <class T>
    static void lookup_enum_str(const VSMap *map, const char *key, const std::unordered_map<std::string, T> &enum_table, std::optional<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            const char *enum_str = propGetScalar<const char *>(map, key, vsapi);
            auto it = enum_table.find(enum_str);
            if (it != enum_table.end())
                *out = it->second;
            else
                throw std::runtime_error{ "bad value: "s + key };
        }
    }

    template <class T>
    static void lookup_enum(const VSMap *map, const char *key, const std::unordered_map<std::string, T> &enum_table, std::optional<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            *out = static_cast<T>(propGetScalar<int>(map, key, vsapi));
        } else {
            std::string altkey = std::string{ key } + "_s";
            lookup_enum_str(map, altkey.c_str(), enum_table, out, vsapi);
        }
    }

    template <class U, class T>
    static void lookup_enum(const VSMap *map, const char *key, const std::unordered_map<std::string, U> &enum_table, const std::unordered_map<U, T> &enum_table_lut, std::optional<T> *out, const VSAPI *vsapi) {
        std::optional<U> vs_value;

        lookup_enum(map, key, enum_table, &vs_value, vsapi);

        if (!vs_value.has_value())
            return;

        auto it = enum_table_lut.find(vs_value.value());
        if (it != enum_table_lut.end())
            *out = it->second;
    }

    template <class T>
    static bool lookup_enum_str_opt(const VSMap *map, const char *key, const std::unordered_map<std::string, T> &enum_table, T *out, const VSAPI *vsapi) {
        std::optional<T> opt;
        lookup_enum_str(map, key, enum_table, &opt, vsapi);
        if (opt.has_value())
            *out = opt.value();
        return opt.has_value();
    }

    template <class T, class U>
    static bool lookup_enum_str_opt(const VSMap *map, const char *key, const std::unordered_map<std::string, U> &enum_table, const std::unordered_map<U, T> &enum_table_lut, T *out, const VSAPI *vsapi) {
        U opt;

        if (!lookup_enum_str_opt(map, key, enum_table, &opt, vsapi))
            return false;
        
        auto it = enum_table_lut.find(opt);
        if (it != enum_table_lut.end()) {
            *out = it->second;
            return true;
        }

        return false;
    }

    template <class T>
    static void propagate_if_present(const std::optional<T> &in, T *out) {
        if (in.has_value())
            *out = in.value();
    }

    vszimg(const VSMap *in, void *userData, VSCore *core, const VSAPI *vsapi)
    {
        vszimg_userdata u{ userData };
        m_field_op = u.op;

        try {
            int err;

            m_node = vsapi->mapGetNode(in, "clip", 0, nullptr);
            const VSVideoInfo &node_vi = *vsapi->getVideoInfo(m_node);

            m_vi = node_vi;

            m_vi.width = propGetScalarDef<unsigned>(in, "width", node_vi.width, vsapi);
            m_vi.height = propGetScalarDef<unsigned>(in, "height", node_vi.height, vsapi);

            if (m_field_op == FieldOp::DEINTERLACE)
                m_vi.height = node_vi.height * 2;

            if (int format_id = propGetScalarDef<int>(in, "format", 0, vsapi)) {
                if (!vsapi->getVideoFormatByID(&m_vi.format, format_id, core) || m_vi.format.colorFamily == cfUndefined)
                    throw std::runtime_error{ "Invalid format id." };
            } else {
                m_vi.format = node_vi.format;
            }

            lookup_enum(in, "matrix", g_matrix_table, &m_frame_params.matrix, vsapi);
            lookup_enum(in, "transfer", g_transfer_table, &m_frame_params.transfer, vsapi);
            lookup_enum(in, "primaries", g_primaries_table, &m_frame_params.primaries, vsapi);
            lookup_enum(in, "range", g_range_table, h_range_table, &m_frame_params.fullrange, vsapi);
            lookup_enum(in, "chromaloc", g_chromaloc_table, h_chromaloc_table, &m_frame_params.chromaloc, vsapi);

            lookup_enum(in, "matrix_in", g_matrix_table, &m_frame_params_in.matrix, vsapi);
            lookup_enum(in, "transfer_in", g_transfer_table, &m_frame_params_in.transfer, vsapi);
            lookup_enum(in, "primaries_in", g_primaries_table, &m_frame_params_in.primaries, vsapi);
            lookup_enum(in, "range_in", g_range_table, h_range_table, &m_frame_params_in.fullrange, vsapi);
            lookup_enum(in, "chromaloc_in", g_chromaloc_table, h_chromaloc_table, &m_frame_params_in.chromaloc, vsapi);

            if (u.custom) {
                unsigned taps = propGetScalar<unsigned>(in, "taps", vsapi);
                m_custom_kernel = vsapi->mapGetFunction(in, "custom_kernel", 0, &err);

                try {
                    for (int i = 0; i < 2; i++)
                        filters[i] = std::make_unique<CustomZimgFilter>(taps, vsapi->addFunctionRef(m_custom_kernel), vsapi);
                } catch (const std::bad_alloc &) {
                    zimg::error::throw_<zimg::error::OutOfMemory>();
                }
            } else {
                zimg_resample_filter_e resample_filter_uv = u.filter;
                double filter_param_a_uv, filter_param_b_uv;

                double filter_param_a = propGetScalarDef<double>(in, "filter_param_a", NAN, vsapi);
                double filter_param_b = propGetScalarDef<double>(in, "filter_param_b", NAN, vsapi);

                if (lookup_enum_str_opt(in, "resample_filter_uv", g_resample_filter_table, &resample_filter_uv, vsapi)) {
                    filter_param_a_uv = propGetScalarDef<double>(in, "filter_param_a_uv", NAN, vsapi);
                    filter_param_b_uv = propGetScalarDef<double>(in, "filter_param_b_uv", NAN, vsapi);
                } else {
                    filter_param_a_uv = filter_param_a;
                    filter_param_b_uv = filter_param_b;
                }

                filters[0] = translate_resize_filter(u.filter, filter_param_a, filter_param_b);
                filters[1] = translate_resize_filter(resample_filter_uv, filter_param_a_uv, filter_param_b_uv);
            }

            lookup_enum_str_opt(in, "dither_type", g_dither_type_table, h_dither_type_table, &m_params.dither_type, vsapi);
            lookup_enum_str_opt(in, "cpu_type", g_cpu_type_table, h_cpu_type_table, &m_params.cpu_type, vsapi);

            if (vsapi->mapNumElements(in, "prefer_props") >= 0)
                vsapi->logMessage(mtWarning, "The deprecated argument prefer_props was passed to a resizer. Ignoring argument.", core);

            m_src_left = propGetScalarDef<double>(in, "src_left", NAN, vsapi);
            m_src_top = propGetScalarDef<double>(in, "src_top", NAN, vsapi);
            m_src_width = propGetScalarDef<double>(in, "src_width", NAN, vsapi);
            m_src_height = propGetScalarDef<double>(in, "src_height", NAN, vsapi);
            m_params.nominal_peak_luminance = propGetScalarDef<double>(in, "nominal_luminance", NAN, vsapi);

            if (vsapi->mapGetInt(in, "force", 0, &err)) {
                m_params.force.force_h = true;
                m_params.force.force_v = true;
            } else {
                m_params.force.force_h = !!vsapi->mapGetInt(in, "force_h", 0, &err);
                m_params.force.force_v = !!vsapi->mapGetInt(in, "force_v", 0, &err);
            }

            // Basic compatibility check.
            if (isConstantVideoFormat(&node_vi) && isConstantVideoFormat(&m_vi)) {
                vsrz_image_format src_format, dst_format;

                src_format.width = node_vi.width;
                src_format.height = node_vi.height;
                dst_format.width = m_vi.width;
                dst_format.height = m_vi.height;

                translate_vsformat(&node_vi.format, &src_format, vsapi);
                translate_vsformat(&m_vi.format, &dst_format, vsapi);

                if ((dst_format.color_family == GraphBuilder::ColorFamily::YUV || dst_format.color_family == GraphBuilder::ColorFamily::GREY)
                    && dst_format.matrix_coefficients == zimg_matrix_coefficients_e::ZIMG_MATRIX_UNSPECIFIED
                    && src_format.color_family != GraphBuilder::ColorFamily::YUV
                    && src_format.color_family != GraphBuilder::ColorFamily::GREY
                    && !m_frame_params.matrix.has_value()) {
                    throw std::runtime_error{ "Matrix must be specified when converting to YUV or GRAY from RGB" };
                }
            }
        } catch (...) {
            freeResources(core, vsapi);
            throw;
        }
    }

    std::shared_ptr<graph_data> get_graph_data(const vsrz_image_format &src_format, const vsrz_image_format &dst_format) {
        std::shared_ptr<graph_data> *data_ptr;

        if (src_format.field_parity == GraphBuilder::FieldParity::TOP)
            data_ptr = &m_graph_data_t;
        else if (src_format.field_parity == GraphBuilder::FieldParity::BOTTOM)
            data_ptr = &m_graph_data_b;
        else
            data_ptr = &m_graph_data_p;

        std::shared_ptr<graph_data> data = std::atomic_load(data_ptr);
        if (!data || data->src_format != src_format || data->dst_format != dst_format) {
            data = std::make_shared<graph_data>(src_format, dst_format, m_params, filters);
            std::atomic_store(data_ptr, data);
        }

        return data;
    }

    void set_frame_params(const frame_params &params, vsrz_image_format *format) {
        propagate_if_present(params.matrix, &format->matrix_coefficients);
        propagate_if_present(params.transfer, &format->transfer_characteristics);
        propagate_if_present(params.primaries, &format->color_primaries);
        propagate_if_present(params.fullrange, &format->fullrange);
        propagate_if_present(params.chromaloc, &format->chroma_location);
    }

    void set_src_colorspace(const VSMap *props, vsrz_image_format *src_format, bool *interlaced, const VSAPI *vsapi) {
        // Frame properties take precedence over defaults.
        set_frame_params(m_frame_params_in, src_format);
        import_frame_props(props, src_format, interlaced, vsapi);
    }

    void set_dst_colorspace(const vsrz_image_format &src_format, vsrz_image_format *dst_format) {
        // Avoid copying matrix coefficients when restricted by color family.
        if (dst_format->matrix_coefficients != zimg_matrix_coefficients_e::ZIMG_MATRIX_RGB)
            dst_format->matrix_coefficients = src_format.matrix_coefficients;

        dst_format->transfer_characteristics = src_format.transfer_characteristics;
        dst_format->color_primaries = src_format.color_primaries;

        // Avoid propagating source pixel range and chroma location if color family changes.
        if (dst_format->color_family == src_format.color_family) {
            dst_format->fullrange = src_format.fullrange;

            if (dst_format->color_family == GraphBuilder::ColorFamily::YUV &&
                (dst_format->subsample_w || dst_format->subsample_h) &&
                (src_format.subsample_w || src_format.subsample_h))
            {
                dst_format->chroma_location = src_format.chroma_location;
            }
        }

        dst_format->field_parity = src_format.field_parity;
        set_frame_params(m_frame_params, dst_format);
    }

    const VSFrame *real_get_frame(const VSFrame *src_frame, VSCore *core, const VSAPI *vsapi) {
        VSFrame *dst_frame = nullptr;
        vsrz_image_format src_format, dst_format;

        try {
            const VSMap *src_props = vsapi->getFramePropertiesRO(src_frame);
            const VSVideoFormat *src_vsformat = vsapi->getVideoFrameFormat(src_frame);
            const VSVideoFormat *dst_vsformat = (m_vi.format.colorFamily != cfUndefined) ? &m_vi.format : src_vsformat;

            src_format.width = vsapi->getFrameWidth(src_frame, 0);
            src_format.height = vsapi->getFrameHeight(src_frame, 0);
            dst_format.width = m_vi.width ? static_cast<unsigned>(m_vi.width) : src_format.width;
            dst_format.height = m_vi.height ? static_cast<unsigned>(m_vi.height) : src_format.height;

            src_format.active_region.left = m_src_left;
            src_format.active_region.top = m_src_top;
            src_format.active_region.width = m_src_width;
            src_format.active_region.height = m_src_height;

            translate_vsformat(src_vsformat, &src_format, vsapi);
            translate_vsformat(dst_vsformat, &dst_format, vsapi);

            bool interlaced = false;

            set_frame_params(m_frame_params_in, &src_format);
            import_frame_props(src_props, &src_format, &interlaced, vsapi);
            set_dst_colorspace(src_format, &dst_format);

            if (m_field_op == FieldOp::DEINTERLACE) {
                if (interlaced || src_format.field_parity == GraphBuilder::FieldParity::PROGRESSIVE)
                    vsapi->logMessage(mtFatal, "expected _Field when bobbing", core);

                dst_format.height = src_format.height * 2;
                dst_format.field_parity = GraphBuilder::FieldParity::PROGRESSIVE;
            }

            if (!m_params.force && src_format == dst_format && isSameVideoFormat(src_vsformat, dst_vsformat) && !is_shifted(src_format)) {
                VSFrame *clone = vsapi->copyFrame(src_frame, core);
                export_frame_props(dst_format, vsapi->getFramePropertiesRW(clone), vsapi);
                return clone;
            }

            dst_frame = vsapi->newVideoFrame(dst_vsformat, dst_format.width, dst_format.height, src_frame, core);

            if (interlaced) {
                vsrz_image_format src_format_t = src_format;
                vsrz_image_format dst_format_t = dst_format;

                src_format_t.height /= 2;
                dst_format_t.height /= 2;

                src_format_t.field_parity = GraphBuilder::FieldParity::TOP;
                dst_format_t.field_parity = GraphBuilder::FieldParity::TOP;
                std::shared_ptr<graph_data> graph_t = get_graph_data(src_format_t, dst_format_t);

                vsrz_image_format src_format_b = src_format_t;
                vsrz_image_format dst_format_b = dst_format_t;
                src_format_b.field_parity = GraphBuilder::FieldParity::BOTTOM;
                dst_format_b.field_parity = GraphBuilder::FieldParity::BOTTOM;
                std::shared_ptr<graph_data> graph_b = get_graph_data(src_format_b, dst_format_b);

                std::unique_ptr<void, decltype(&vsh_aligned_free)> tmp{
                    vsh_aligned_malloc(std::max(graph_t->graph.get_tmp_size(), graph_b->graph.get_tmp_size()), 64),
                    vsh_aligned_free
                };
                if (!tmp)
                    throw std::bad_alloc{};

                auto src_buffer = import_frame_as_buffer_const(src_frame, vsapi);
                auto dst_buffer = import_frame_as_buffer(dst_frame, vsapi);

                auto src_buffer_b = get_field_buffer(src_buffer, src_vsformat->numPlanes, GraphBuilder::FieldParity::BOTTOM);
                auto dst_buffer_b = get_field_buffer(dst_buffer, dst_vsformat->numPlanes, GraphBuilder::FieldParity::BOTTOM);
                graph_b->graph.process(src_buffer_b, dst_buffer_b, tmp.get());

                auto src_buffer_t = get_field_buffer(src_buffer, src_vsformat->numPlanes, GraphBuilder::FieldParity::TOP);
                auto dst_buffer_t = get_field_buffer(dst_buffer, dst_vsformat->numPlanes, GraphBuilder::FieldParity::TOP);
                graph_t->graph.process(src_buffer_t, dst_buffer_t, tmp.get());
            } else {
                std::shared_ptr<graph_data> graph = get_graph_data(src_format, dst_format);

                std::unique_ptr<void, decltype(&vsh_aligned_free)> tmp{
                    vsh_aligned_malloc(graph->graph.get_tmp_size(), 64),
                    vsh_aligned_free
                };
                if (!tmp)
                    throw std::bad_alloc{};

                auto src_buffer = import_frame_as_buffer_const(src_frame, vsapi);
                auto dst_buffer = import_frame_as_buffer(dst_frame, vsapi);
                graph->graph.process(src_buffer, dst_buffer, tmp.get());
            }

            VSMap *dst_props = vsapi->getFramePropertiesRW(dst_frame);
            propagate_sar(src_props, dst_props, src_format, dst_format, vsapi);
            export_frame_props(dst_format, dst_props, vsapi);
        } catch (const vszimgxx::zerror &e) {
            vsapi->freeFrame(dst_frame);

            if (e.code == ZIMG_ERROR_NO_COLORSPACE_CONVERSION) {
                char buf[256];

                snprintf(buf, sizeof(buf), "Resize error %d: %s (%d/%d/%d => %d/%d/%d). May need to specify additional colorspace parameters.",
                    e.code, e.msg, src_format.matrix_coefficients, src_format.transfer_characteristics, src_format.color_primaries,
                    dst_format.matrix_coefficients, dst_format.transfer_characteristics, dst_format.color_primaries);
                throw std::runtime_error{ buf };
            } else {
                throw;
            }
        } catch (const zimg::error::Exception &e) {
            vsapi->freeFrame(dst_frame);

            throw std::runtime_error{ e.what() };
        } catch (...) {
            vsapi->freeFrame(dst_frame);
            throw;
        }

        return dst_frame;
    }
public:
    ~vszimg() {
        assert(!m_node);
        assert(!m_custom_kernel);
    }

    void freeResources(VSCore *core, const VSAPI *vsapi) {
        vsapi->freeNode(m_node);
        vsapi->freeFunction(m_custom_kernel);
        m_node = nullptr;
        m_custom_kernel = nullptr;
    }

    const VSFrame *get_frame(int n, int activationReason, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        const VSFrame *ret = nullptr;
        const VSFrame *src_frame = nullptr;

        try {
            if (activationReason == arInitial) {
                vsapi->requestFrameFilter(n, m_node, frameCtx);
            } else if (activationReason == arAllFramesReady) {
                src_frame = vsapi->getFrameFilter(n, m_node, frameCtx);
                ret = real_get_frame(src_frame, core, vsapi);
            }
        } catch (const vszimgxx::zerror &e) {
            std::string errmsg = "Resize error " + std::to_string(e.code) + ": " + e.msg;
            vsapi->setFilterError(errmsg.c_str(), frameCtx);
        } catch (const std::exception &e) {
            vsapi->setFilterError(("Resize error: "s + e.what()).c_str(), frameCtx);
        }

        vsapi->freeFrame(src_frame);
        return ret;
    }

    static void VS_CC create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
        try {
            vszimg *x = new vszimg{ in, userData, core, vsapi };
            vszimg_userdata u{ userData };
            const char *name = "";

            if (u.custom) {
                name = "Custom";
            } else if (u.op == FieldOp::DEINTERLACE) {
                name = "Bob";
            } else {
                switch (u.filter) {
                case ZIMG_RESIZE_POINT: name = "Point"; break;
                case ZIMG_RESIZE_BILINEAR: name = "Bilinear"; break;
                case ZIMG_RESIZE_BICUBIC: name = "Bicubic"; break;
                case ZIMG_RESIZE_SPLINE16: name = "Spline16"; break;
                case ZIMG_RESIZE_SPLINE36: name = "Spline36"; break;
                case ZIMG_RESIZE_SPLINE64: name = "Spline64"; break;
                case ZIMG_RESIZE_LANCZOS: name = "Lanczos"; break;
                }
            }

            VSFilterDependency deps[] = {{x->m_node, rpStrictSpatial}};
            vsapi->createVideoFilter(out, name, &x->m_vi, &vszimg::static_get_frame, &vszimg::free, fmParallel, deps, 1, x, core);
        } catch (const vszimgxx::zerror &e) {
            std::string errmsg = "Resize error " + std::to_string(e.code) + ": " + e.msg;
            vsapi->mapSetError(out, errmsg.c_str());
        } catch (const std::exception &e) {
            vsapi->mapSetError(out, ("Resize error: "s + e.what()).c_str());
        }
    }

    static void VS_CC free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
        vszimg *ptr = static_cast<vszimg *>(instanceData);
        ptr->freeResources(core, vsapi);
        delete ptr;
    }

    static const VSFrame * VS_CC static_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        return static_cast<vszimg *>(instanceData)->get_frame(n, activationReason, frameData, frameCtx, core, vsapi);
    }
};


void VS_CC bobCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) noexcept {
    vszimg_userdata u{ userData };
    u.op = FieldOp::DEINTERLACE;

    VSPlugin *stdplugin = vsapi->getPluginByNamespace("std", core);
    VSMap *tmp_map = nullptr;
    VSMap *sep_fields = nullptr;
    int _;

    if (const char *filterName = vsapi->mapGetData(in, "filter", 0, &_)) {
        auto it = g_resample_filter_table.find(filterName);

        if (it != g_resample_filter_table.end())
            u.filter = it->second;
    }

    tmp_map = vsapi->createMap();
    vsapi->mapConsumeNode(tmp_map, "clip", vsapi->mapGetNode(in, "clip", 0, nullptr), maReplace);
    if (vsapi->mapNumElements(in, "tff") > 0)
        vsapi->mapSetInt(tmp_map, "tff", vsapi->mapGetInt(in, "tff", 0, nullptr), maReplace);
    sep_fields = vsapi->invoke(stdplugin, "SeparateFields", tmp_map);
    if (const char *err = vsapi->mapGetError(sep_fields)) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

    vsapi->copyMap(in, tmp_map);
    vsapi->mapDeleteKey(tmp_map, "filter");
    vsapi->mapDeleteKey(tmp_map, "tff");
    vsapi->mapConsumeNode(tmp_map, "clip", vsapi->mapGetNode(sep_fields, "clip", 0, nullptr), maReplace);
    vszimg::create(tmp_map, out, u, core, vsapi);
fail:
    vsapi->freeMap(tmp_map);
    vsapi->freeMap(sep_fields);
}

} // namespace

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(
        "dev.setsugen.resize2", "resize2", "Built-in VapourSynth resizer based on zimg with some modifications.",
        VS_MAKE_VERSION(2, 0), VAPOURSYNTH_API_VERSION, 0, plugin
    );

#define INT_OPT(x) #x ":int:opt;"
#define FLOAT_OPT(x) #x ":float:opt;"
#define DATA_OPT(x) #x ":data:opt;"
#define ENUM_OPT(x) INT_OPT(x) DATA_OPT(x ## _s)

#define COMMON_ARGS \
  INT_OPT(format) \
  ENUM_OPT(matrix) \
  ENUM_OPT(transfer) \
  ENUM_OPT(primaries) \
  ENUM_OPT(range) \
  ENUM_OPT(chromaloc) \
  ENUM_OPT(matrix_in) \
  ENUM_OPT(transfer_in) \
  ENUM_OPT(primaries_in) \
  ENUM_OPT(range_in) \
  ENUM_OPT(chromaloc_in)

#define COMMON_FILTER_ARGS \
  FLOAT_OPT(filter_param_a) \
  FLOAT_OPT(filter_param_b) \
  DATA_OPT(resample_filter_uv) \
  FLOAT_OPT(filter_param_a_uv) \
  FLOAT_OPT(filter_param_b_uv)

#define COMMON_OTHER_ARGS \
  DATA_OPT(dither_type) \
  DATA_OPT(cpu_type) \
  INT_OPT(prefer_props) \
  FLOAT_OPT(src_left) \
  FLOAT_OPT(src_top) \
  FLOAT_OPT(src_width) \
  FLOAT_OPT(src_height) \
  FLOAT_OPT(nominal_luminance) \
  INT_OPT(force) \
  INT_OPT(force_h) \
  INT_OPT(force_v) \

    static const char RESAMPLE_ARGS[] =
        "clip:vnode;"
        INT_OPT(width)
        INT_OPT(height)
        COMMON_ARGS
        COMMON_FILTER_ARGS
        COMMON_OTHER_ARGS;

    static const char RESAMPLE_CUSTOM_ARGS[] =
        "clip:vnode;"
        "custom_kernel:func;"
        "taps:int;"
        INT_OPT(width)
        INT_OPT(height)
        COMMON_ARGS
        COMMON_OTHER_ARGS;

    static const char RETURN_VALUE[] = "clip:vnode;";

    vspapi->registerFunction("Bilinear", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_BILINEAR), plugin);
    vspapi->registerFunction("Bicubic", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_BICUBIC), plugin);
    vspapi->registerFunction("Point", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_POINT), plugin);
    vspapi->registerFunction("Lanczos", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_LANCZOS), plugin);
    vspapi->registerFunction("Spline16", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE16), plugin);
    vspapi->registerFunction("Spline36", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE36), plugin);
    vspapi->registerFunction("Spline64", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE64), plugin);

    vspapi->registerFunction("Custom", RESAMPLE_CUSTOM_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_BICUBIC, true), plugin);

    vspapi->registerFunction("Bob", "clip:vnode;filter:data:opt;tff:int:opt;" COMMON_ARGS, RETURN_VALUE, bobCreate, vszimg_userdata(ZIMG_RESIZE_BICUBIC), plugin);

#undef COMMON_ARGS
#undef COMMON_FILTER_ARGS
#undef COMMON_OTHER_ARGS
#undef INT_OPT
#undef FLOAT_OPT
#undef DATA_OPT
#undef ENUM_OPT
}
