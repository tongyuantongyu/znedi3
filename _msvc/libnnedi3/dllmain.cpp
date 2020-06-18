// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#include <memory>

#include "alloc.h"

typedef struct Bitmap {
	UINT8* Scan0;
	UINT32 Stride;
	UINT32 Width;
	UINT32 Height;
	UINT32 Depth;
	UINT32 Channel;
} Bitmap;

struct FreeWeights {
	void operator()(znedi3_weights* ptr) const { znedi3_weights_free(ptr); }
};

struct FreeFilter {
	void operator()(znedi3_filter* ptr) const { znedi3_filter_free(ptr); }
};

template <unsigned int planes, typename ChannelType>
struct PlanarImage {
	znedi3::AlignedVector<ChannelType> data[planes];
	ptrdiff_t stride[planes];
	unsigned width[planes];
	unsigned height[planes];

	unsigned planeCount = planes;
	typedef ChannelType PixelChannelType;

	PlanarImage(unsigned width, unsigned height) {
		ptrdiff_t stride = width % 64 ? width - width % 64 + 64 : width;

		for (unsigned p = 0; p < planes; ++p) {
			this->data[p].resize(stride * height);
			this->stride[p] = stride;
			this->width[p] = width;
			this->height[p] = height;
		}
	}
};

template <unsigned int planes, typename ChannelType>
bool CopyToPlanarImageTransposeExtend(PlanarImage<planes, ChannelType>& pImage, Bitmap* src) {
	if (src->Channel != planes || sizeof(ChannelType) << 3 != src->Depth) {
		return false;
	}

	if (pImage.width[0] != src->Height || pImage.height[0] != 2 * src->Width) {
		return false;
	}

	for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(src->Height); ++i) {
		const ChannelType* src_p = reinterpret_cast<ChannelType*>(src->Scan0 + i * src->Stride);
		constexpr auto step = planes;

		if constexpr (planes == 3) {
			for (unsigned j = 0; j < src->Width; ++j) {
				pImage.data[0][2 * j * pImage.stride[0] + i] = src_p[j * step + 2];
				pImage.data[1][2 * j * pImage.stride[1] + i] = src_p[j * step + 1];
				pImage.data[2][2 * j * pImage.stride[2] + i] = src_p[j * step + 0];
			}
		}
		else {
			for (unsigned j = 0; j < src->Width; ++j) {
				pImage.data[0][2 * j * pImage.stride[0] + i] = src_p[j * step + 2];
				pImage.data[1][2 * j * pImage.stride[1] + i] = src_p[j * step + 1];
				pImage.data[2][2 * j * pImage.stride[2] + i] = src_p[j * step + 0];
				pImage.data[3][2 * j * pImage.stride[3] + i] = src_p[j * step + 3];
			}
		}
	}

	return true;
}

template <unsigned int planes, typename ChannelType>
bool CopyToPlanarImageTransposeExtend(PlanarImage<planes, ChannelType>& pImage, PlanarImage<planes, ChannelType>& src) {
	if (pImage.width[0] != src.height[0] || pImage.height[0] != 2 * src.width[0]) {
		return false;
	}
	
	for (unsigned int p = 0; p < planes; ++p) {
		for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(src.height[p]); ++i) {
			for (unsigned j = 0; j < src.width[p]; ++j) {
				pImage.data[p][2 * j * pImage.stride[p] + i] = src.data[p][i * src.stride[p] + j];
			}
		}
	}

	return true;
}

template <unsigned int planes, typename ChannelType>
bool CopyToBitmap(PlanarImage<planes, ChannelType>& pImage, Bitmap* dst) {
	if (dst->Channel != planes || sizeof(ChannelType) << 3 != dst->Depth) {
		return false;
	}

	if (pImage.width[0] != dst->Width || pImage.height[0] != dst->Height) {
		return false;
	}

	for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(dst->Height); ++i) {
		const ChannelType* src_p = reinterpret_cast<ChannelType*>(dst->Scan0 + i * dst->Stride);
		constexpr auto step = planes;

		if constexpr (planes == 3) {
			for (unsigned j = 0; j < dst->Width; ++j) {
				pImage.data[0][i * pImage.stride[0] + j] = src_p[j * step + 2];
				pImage.data[1][i * pImage.stride[1] + j] = src_p[j * step + 1];
				pImage.data[2][i * pImage.stride[2] + j] = src_p[j * step + 0];
			}
		}
		else {
			for (unsigned j = 0; j < dst->Width; ++j) {
				pImage.data[0][i * pImage.stride[0] + j] = src_p[j * step + 2];
				pImage.data[1][i * pImage.stride[1] + j] = src_p[j * step + 1];
				pImage.data[2][i * pImage.stride[2] + j] = src_p[j * step + 0];
				pImage.data[3][i * pImage.stride[3] + j] = src_p[j * step + 3];
			}
		}
	}

	return true;
}

template <unsigned int planes, typename ChannelType>
void execute(const znedi3_filter* filter,
             const PlanarImage<planes, ChannelType>& in,
             PlanarImage<planes, ChannelType>& out) {
	znedi3::AlignedVector<unsigned char> tmp(
		znedi3_filter_get_tmp_size(filter, in.width[0], in.height[0] / 2));

	for (unsigned p = 0; p < in.planeCount; ++p) {
		// znedi3_filter_process(filter, out.width[p], out.height[p] / 2,
		//                       in.data[p].data(), in.stride[p] * 2 * sizeof(ChannelType),
		//                       out.data[p].data() + out.stride[p], out.stride[p] * 2 * sizeof(ChannelType),
		//                       tmp.data(), 1);
		znedi3_filter_process(filter, out.width[p], out.height[p] / 2,
		                      in.data[p].data(), in.stride[p] * 2,
		                      out.data[p].data() + out.stride[p], out.stride[p] * 2,
		                      tmp.data(), 1);
	}
}

std::unique_ptr<znedi3_weights, FreeWeights> weights;

BOOL APIENTRY DllMain(HMODULE, const DWORD ul_reason_for_call, LPVOID) {
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		weights = std::unique_ptr<znedi3_weights, FreeWeights>{znedi3_weights_from_file("nnedi3_weights.bin")};
		return !!weights;
	}
	if (ul_reason_for_call == DLL_PROCESS_DETACH) {
		weights = nullptr;
	}
	return true;
}

#define export __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif

export bool DoubleImage(Bitmap* src, Bitmap* dst) {
	znedi3_filter_params params;
	znedi3_filter_params_default(&params);

	params.pixel_type = src->Depth == 16 ? ZNEDI3_PIXEL_HALF : ZNEDI3_PIXEL_BYTE;
	const std::unique_ptr<znedi3_filter, FreeFilter> filter{znedi3_filter_create(weights.get(), &params)};

	if (!filter) {
		return false;
	}

	if (dst->Width != src->Width * 2 || dst->Height != src->Width * 2) {
		return false;
	}

	if (src->Channel == 3) {
		if (src->Depth == 8) {
			PlanarImage<3, UINT8> source1{src->Height, src->Width * 2};
			if (!CopyToPlanarImageTransposeExtend(source1, src)) {
				return false;
			}
			auto result1 = source1;
			execute(filter.get(), source1, result1);
			PlanarImage<3, UINT8> source2{src->Width * 2, src->Height * 2};
			if (!CopyToPlanarImageTransposeExtend(source2, source1)) {
				return false;
			}
			auto result2 = source2;
			execute(filter.get(), source2, result2);
			return CopyToBitmap(result2, dst);
		}
		if (src->Depth == 16) {
			PlanarImage<3, UINT16> source1{src->Height, src->Width * 2};
			if (!CopyToPlanarImageTransposeExtend(source1, src)) {
				return false;
			}
			auto result1 = source1;
			execute(filter.get(), source1, result1);
			PlanarImage<3, UINT16> source2{src->Width * 2, src->Height * 2};
			if (!CopyToPlanarImageTransposeExtend(source2, source1)) {
				return false;
			}
			auto result2 = source2;
			execute(filter.get(), source2, result2);
			return CopyToBitmap(result2, dst);
		}
	}
	else if (src->Channel == 4) {
		if (src->Depth == 8) {
			PlanarImage<4, UINT8> source1{src->Height, src->Width * 2};
			if (!CopyToPlanarImageTransposeExtend(source1, src)) {
				return false;
			}
			auto result1 = source1;
			execute(filter.get(), source1, result1);
			PlanarImage<4, UINT8> source2{src->Width * 2, src->Height * 2};
			if (!CopyToPlanarImageTransposeExtend(source2, source1)) {
				return false;
			}
			auto result2 = source2;
			execute(filter.get(), source2, result2);
			return CopyToBitmap(result2, dst);
		}
		if (src->Depth == 16) {
			PlanarImage<4, UINT16> source1{src->Height, src->Width * 2};
			if (!CopyToPlanarImageTransposeExtend(source1, src)) {
				return false;
			}
			auto result1 = source1;
			execute(filter.get(), source1, result1);
			PlanarImage<4, UINT16> source2{src->Width * 2, src->Height * 2};
			if (!CopyToPlanarImageTransposeExtend(source2, source1)) {
				return false;
			}
			auto result2 = source2;
			execute(filter.get(), source2, result2);
			return CopyToBitmap(result2, dst);
		}
	}

	return false;
}
	
#ifdef __cplusplus
}
#endif
