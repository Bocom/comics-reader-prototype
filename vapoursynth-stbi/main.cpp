
#include "VapourSynth.h"
#include "VSHelper.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct {
	VSVideoInfo vi;
	char *filename;
} stbImageData;

static void VS_CC filterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	stbImageData *d = (stbImageData *)* instanceData;
	vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC filterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	stbImageData *d = (stbImageData *)* instanceData;

	if (activationReason == arInitial)
	{
		VSFrameRef *frame = nullptr;

		int width, height;
		auto image_data = stbi_load(d->filename, &width, &height, nullptr, 3);
		if (!image_data)
		{
			vsapi->setFilterError("Image: Somehow the file couldn't be found.", frameCtx);
			return nullptr;
		}

		frame = vsapi->newVideoFrame(d->vi.format, width, height, nullptr, core);

		int stride = vsapi->getStride(frame, 0);

		uint8_t *r = vsapi->getWritePtr(frame, 0);
		uint8_t *g = vsapi->getWritePtr(frame, 1);
		uint8_t *b = vsapi->getWritePtr(frame, 2);

		uint8_t *ptr = image_data;

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				r[x] = *(ptr++);
				g[x] = *(ptr++);
				b[x] = *(ptr++);
			}

			r += stride;
			g += stride;
			b += stride;
		}

		return frame;
	}
	return nullptr;
}

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	stbImageData *d = (stbImageData *)instanceData;
	free(d->filename);
	free(d);
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	stbImageData d = { nullptr };
	stbImageData *data;

	const char *filename = vsapi->propGetData(in, "filename", 0, nullptr);

	int width = 0, height = 0;
	stbi_load(filename, &width, &height, nullptr, 3);
	if (width == 0 || height == 0)
	{
		vsapi->setError(out, "Image: Couldn't open the file.");
		return;
	}

	int count = strlen(filename) + 1;
	d.filename = (char *)malloc(count * sizeof(char));
	strcpy_s(d.filename, count, filename);

	d.vi = { nullptr, 30, 1, width, height, 1, 0 };

	d.vi.format = vsapi->getFormatPreset(pfRGB24, core);

	data = (stbImageData *)malloc(sizeof(d));
	*data = d;

	vsapi->createFilter(in, out, "Image", filterInit, filterGetFrame, filterFree, fmParallel, 0, data, core);
}


VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.bocom.stb", "stb", "stb_image Image Loder", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("Image", "filename:data;", filterCreate, nullptr, plugin);
}
