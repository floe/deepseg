/* This is licenced software, @see LICENSE file.
 * Authors - @see AUTHORS file.
==============================================================================*/

// tested against tensorflow lite v2.4.1 (static library)

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/optional_debug_tools.h"

#include "transpose_conv_bias.h"
#include "libbackscrub.h"

// Debug helper
static void _dbg(calcinfo_t &info, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	if (info.ondebug)
		info.ondebug(info.caller_ctx, fmt, ap);
	else
		vfprintf(stderr, fmt, ap);
	va_end(ap);
}

// Tensorflow Lite helper functions
enum class modeltype_t {
	Unknown,
	BodyPix,
	DeepLab,
	GoogleMeetSegmentation,
	MLKitSelfie,
};

struct normalization_t {
	float scaling;
	float offset;
};

struct backscrub_ctx_t {
	std::unique_ptr<tflite::FlatBufferModel> model;
	std::unique_ptr<tflite::Interpreter> interpreter;
	modeltype_t modeltype;
	normalization_t norm;
};

static cv::Mat getTensorMat(calcinfo_t &info, int tnum) {

	backscrub_ctx_t &ctx = *((backscrub_ctx_t *)info.backscrub_ctx);
	TfLiteType t_type = ctx.interpreter->tensor(tnum)->type;
	TFLITE_MINIMAL_CHECK(t_type == kTfLiteFloat32);

	TfLiteIntArray* dims = ctx.interpreter->tensor(tnum)->dims;
	if (info.debug)
		for (int i = 0; i < dims->size; i++)
			_dbg(info,"tensor #%d: %d\n",tnum,dims->data[i]);
	TFLITE_MINIMAL_CHECK(dims->data[0] == 1);

	int h = dims->data[1];
	int w = dims->data[2];
	int c = dims->data[3];

	float* p_data = ctx.interpreter->typed_tensor<float>(tnum);
	TFLITE_MINIMAL_CHECK(p_data != nullptr);

	return cv::Mat(h,w,CV_32FC(c),p_data);
}

// Determine type of model from the name
// TODO:XXX: use metadata when available
static modeltype_t get_modeltype(const char* modelname) {
	if (strstr(modelname, "body-pix")) {
		return modeltype_t::BodyPix;
	}
	else if (strstr(modelname, "deeplab")) {
		return modeltype_t::DeepLab;
	}
	else if (strstr(modelname, "segm_")) {
		return modeltype_t::GoogleMeetSegmentation;
	}
	else if (strstr(modelname, "selfie")) {
		return modeltype_t::MLKitSelfie;
	}
	return modeltype_t::Unknown;
}

static normalization_t get_normalization(modeltype_t type) {
	// TODO: This should be read out from actual mode metadata instead
	switch (type) {
		case modeltype_t::DeepLab:
			return normalization_t{.scaling = 1/127.5, .offset = -1};
		case modeltype_t::BodyPix:
		case modeltype_t::GoogleMeetSegmentation:
		case modeltype_t::MLKitSelfie:
		case modeltype_t::Unknown:
		default:
			return normalization_t{.scaling = 1/255.0, .offset = 0};
	}
}

// deeplabv3 classes
static const std::vector<std::string> labels = { "background", "aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat", "chair", "cow", "dining table", "dog", "horse", "motorbike", "person", "potted plant", "sheep", "sofa", "train", "tv" };
// label number of "person" for DeepLab v3+ model
static const size_t cnum = labels.size();
static const size_t pers = std::distance(labels.begin(), std::find(labels.begin(),labels.end(),"person"));

int init_tensorflow(calcinfo_t &info) {
	// Deallocate if not done already
	if (info.backscrub_ctx)
		drop_tensorflow(info);
	// Allocate context
	info.backscrub_ctx = new backscrub_ctx_t;
	// Take a reference so we can write tidy code with ctx.<x>
	backscrub_ctx_t &ctx = *((backscrub_ctx_t *)info.backscrub_ctx);
	// Load model
	ctx.model = tflite::FlatBufferModel::BuildFromFile(info.modelname);
	TFLITE_MINIMAL_CHECK(ctx.model != nullptr);
	// Determine model type and normalization values
	ctx.modeltype = get_modeltype(info.modelname);
	ctx.norm = get_normalization(ctx.modeltype);
	if (modeltype_t::Unknown == ctx.modeltype) {
		_dbg(info, "Unknown model type '%s'.\n", info.modelname);
		return 0;
	}
	// Build the interpreter
	tflite::ops::builtin::BuiltinOpResolver resolver;
	// custom op for Google Meet network
	resolver.AddCustom("Convolution2DTransposeBias", mediapipe::tflite_operations::RegisterConvolution2DTransposeBias());
	tflite::InterpreterBuilder builder(*ctx.model, resolver);
	builder(&ctx.interpreter);
	TFLITE_MINIMAL_CHECK(ctx.interpreter != nullptr);

	// Allocate tensor buffers.
	TFLITE_MINIMAL_CHECK(ctx.interpreter->AllocateTensors() == kTfLiteOk);

	// set interpreter params
	ctx.interpreter->SetNumThreads(info.threads);
	ctx.interpreter->SetAllowFp16PrecisionForFp32(true);

	// get input and output tensor as cv::Mat
	info.input = getTensorMat(info, ctx.interpreter->inputs ()[0]);
	info.output = getTensorMat(info, ctx.interpreter->outputs()[0]);
	info.ratio = (float)info.input.cols/(float) info.input.rows;

	// initialize mask and square ROI in center
	info.roidim = cv::Rect((info.width-info.height/info.ratio)/2,0,info.height/info.ratio,info.height);
	info.mask = cv::Mat::ones(info.height,info.width,CV_8UC1)*255;
	info.mroi = info.mask(info.roidim);

	// mask blurring size
	info.blur = cv::Size(5,5);

	// create Mat for small mask
	info.ofinal = cv::Mat(info.output.rows,info.output.cols,CV_8UC1);
	return 1;
}

void drop_tensorflow(calcinfo_t &info) {
	if (info.debug)
		_dbg(info, "dropping tensorflow context\n");
	// clear all mask data
	info.ofinal.deallocate();
	info.mask.deallocate();
	info.input.deallocate();
	info.output.deallocate();
	// clear internal context if present
	if (info.backscrub_ctx) {
		backscrub_ctx_t &ctx = *((backscrub_ctx_t *)info.backscrub_ctx);
		// drop interpreter
		ctx.interpreter.reset();
		// drop model
		ctx.model.reset();
		// drop context
		delete &ctx;
		info.backscrub_ctx = nullptr;
	}
}

int calc_mask(calcinfo_t &info) {
	// Ensure we have a context from init_tensorflow()
	TFLITE_MINIMAL_CHECK(info.backscrub_ctx!=NULL);
	backscrub_ctx_t &ctx = *((backscrub_ctx_t *)info.backscrub_ctx);

	// map ROI
	cv::Mat roi = info.raw(info.roidim);

	// resize ROI to input size
	cv::Mat in_u8_bgr, in_u8_rgb;
	cv::resize(roi,in_u8_bgr,cv::Size(info.input.cols,info.input.rows));
	cv::cvtColor(in_u8_bgr,in_u8_rgb,CV_BGR2RGB);
	// TODO: can convert directly to float?

	// bilateral filter to reduce noise
	if (1) {
		cv::Mat filtered;
		cv::bilateralFilter(in_u8_rgb,filtered,5,100.0,100.0);
		in_u8_rgb = filtered;
	}

	// convert to float and normalize values expected by the model
	in_u8_rgb.convertTo(info.input,CV_32FC3,ctx.norm.scaling,ctx.norm.offset);
	if (info.onprep)
		info.onprep(info.caller_ctx);

	// Run inference
	TFLITE_MINIMAL_CHECK(ctx.interpreter->Invoke() == kTfLiteOk);
	if (info.oninfer)
		info.oninfer(info.caller_ctx);

	float* tmp = (float*)info.output.data;
	uint8_t* out = (uint8_t*)info.ofinal.data;

	switch (ctx.modeltype) {
		case modeltype_t::DeepLab:
			// find class with maximum probability
			for (unsigned int n = 0; n < info.output.total(); n++) {
				float maxval = -10000; size_t maxpos = 0;
				for (size_t i = 0; i < cnum; i++) {
					if (tmp[n*cnum+i] > maxval) {
						maxval = tmp[n*cnum+i];
						maxpos = i;
					}
				}
				// set mask to 0 where class == person
				uint8_t val = (maxpos==pers ? 0 : 255);
				out[n] = (val & 0xE0) | (out[n] >> 3);
			}
			break;
		case modeltype_t::BodyPix:
		case modeltype_t::MLKitSelfie:
			// threshold probability
			for (unsigned int n = 0; n < info.output.total(); n++) {
				// FIXME: hardcoded threshold
				uint8_t val = (tmp[n] > 0.65 ? 0 : 255);
				out[n] = (val & 0xE0) | (out[n] >> 3);
			}
			break;
		case modeltype_t::GoogleMeetSegmentation:
			/* 256 x 144 x 2 tensor for the full model or 160 x 96 x 2
			 * tensor for the light model with masks for background
			 * (channel 0) and person (channel 1) where values are in
			 * range [MIN_FLOAT, MAX_FLOAT] and user has to apply
			 * softmax across both channels to yield foreground
			 * probability in [0.0, 1.0]. */
			for (unsigned int n = 0; n < info.output.total(); n++) {
				float exp0 = expf(tmp[2*n  ]);
				float exp1 = expf(tmp[2*n+1]);
				float p0 = exp0 / (exp0+exp1);
				float p1 = exp1 / (exp0+exp1);
				uint8_t val = (p0 < p1 ? 0 : 255);
				out[n] = (val & 0xE0) | (out[n] >> 3);
			}
			break;
		case modeltype_t::Unknown:
			fprintf(stderr, "Unknown model type\n");
			break;
	}

	if (info.onmask)
		info.onmask(info.caller_ctx);

	// scale up into full-sized mask
	cv::Mat tmpbuf;
	cv::resize(info.ofinal,tmpbuf,cv::Size(info.raw.rows/info.ratio,info.raw.rows));

	// blur at full size for maximum smoothness
	cv::blur(tmpbuf,info.mroi,info.blur);
	return 1;
}

