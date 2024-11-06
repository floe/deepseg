# BackScrub
(or The Project Formerly Known As DeepBackSub)

## Virtual Video Device for Background Replacement with Deep Semantic Segmentation

![Screenshots with my stupid grinning face](backgrounds/screenshot.jpg)
(Credits for the nice backgrounds to [Mary Sabell](https://dribbble.com/shots/4686178-Bauhaus-Poster) and [PhotoFunia](https://photofunia.com/effects/retro-wave))

## Maintainers

  * Phil Ashby ([@phlash](https://github.com/phlash))
  * Benny Baumann ([@BenBE](https://github.com/BenBE))
  * Florian Echtler ([@floe](https://github.com/floe))

## License

backscrub is licensed under the Apache License 2.0. See LICENSE file for details.

## Building (Debian)

Install dependencies (`sudo apt install libopencv-dev build-essential v4l2loopback-dkms curl`).

Clone this repository with `git clone --recursive https://github.com/floe/backscrub.git`.
To speed up the checkout you can additionally pass `--depth=1` to `git clone`.
This is okay, if you only want to download and build the code, however, for development it is not recommended.

Use `cmake` to build the project: create a subfolder (e.g. `build`), change to that folder and run: `cmake .. && make -j $(nproc || echo 4)`.

**Deprecated**: Another option to build everything is to run `make` in the root directory of the repository. While this will download and build all dependencies, it comes with a few drawbacks like missing support for XNNPACK. Also this might break with newer versions of Tensorflow Lite as upstream support for this option has been removed. Use at you own risk.

## Building (Fedora)

Install dependencies (`sudo dnf install opencv-devel cmake v4l2loopback akmod-v4l2loopback curl`).

Use `./utils/backscrub.sh` to build the project: `./utils/backscrub.sh build`

## Usage

First, load the v4l2loopback module (extra settings needed to make Chrome work):
```
sudo modprobe v4l2loopback devices=1 max_buffers=2 exclusive_caps=1 card_label="VirtualCam" video_nr=10
```
Then, run backscrub (-d -d for full debug, -c for capture device, -v for virtual device, -b for wallpaper):
```
./backscrub -d -d -c /dev/video0 -v /dev/video10 -b ~/wallpapers/forest.jpg
```

Some cameras (like e.g. `Logitec Brio`) need to switch the video source to `MJPG` by passing `-f MJPG` in order for higher resolutions to become available for use.

For regular usage, setup a configuration file `/etc/modprobe.d/v4l2loopback.conf`:
```
# V4L loopback driver
options v4l2loopback max_buffers=2
options v4l2loopback exclusive_caps=1
options v4l2loopback video_nr=10
options v4l2loopback card_label="VirtualCam"
```
To auto-load the driver on startup, create `/etc/modules-load.d/v4l2loopback.conf` with the following content:
```
v4l2loopback
```

## Requirements

Tested with the following dependencies:

  - Ubuntu 20.04, x86-64
    - Linux kernel 5.6 (stock package)
    - OpenCV 4.2.0 (stock package)
    - V4L2-Loopback 0.12.5 (stock package)
    - Tensorflow Lite 2.5.0 (from [repo](https://github.com/tensorflow/tensorflow/tree/v2.5.0/tensorflow/lite))
  - Ubuntu 18.04.5, x86-64
    - Linux kernel 4.15 (stock package)
    - OpenCV 3.2.0 (stock package)
    - V4L2-Loopback 0.10.0 (stock package)
    - Tensorflow Lite 2.1.0 (from [repo](https://github.com/tensorflow/tensorflow/tree/v2.1.0/tensorflow/lite))
  
Tested with the following software:

  - Firefox 
    - 90.0.2 (works)
    - 84.0   (works)
    - 76.0.1 (works)
    - 74.0.1 (works)
  - Skype 
    - 8.67.0.96 (works)
    - 8.60.0.76 (works)
    - 8.58.0.93 (works)
  - guvcview
    - 2.0.6 (works with parameter `-c read`)
    - 2.0.5 (works with parameter `-c read`)
  - Microsoft Teams
    - 1.3.00.30857 (works)
    - 1.3.00.5153 (works)
    - 1.4.00.26453 (works)
  - Chrome
    - 87.0.4280.88 (works)
    - 81.0.4044.138 (works)
  - Zoom - yes, I'm a hypocrite, I tested it with Zoom after all :-)
    - 5.4.54779.1115 (works)
    - 5.0.403652.0509 (works)

## Background

In these modern times where everyone is sitting at home and skype-ing/zoom-ing/webrtc-ing all the time, I was a bit annoyed about always showing my messy home office to the world. Skype has a "blur background" feature, but that starts to get boring after a while (and it's less private than I would personally like). Zoom has some background substitution thingy built-in, but I'm not touching that software with a bargepole (and that feature is not available on Linux anyway). So I decided to look into how to roll my own implementation without being dependent on any particular video conferencing software to support this.

This whole shebang involves three main steps with varying difficulty:
  - find person in video (hard)
  - replace background (easy)
  - pipe data to virtual video device (medium)

## Finding person in video

### Attempt 0: Depth camera (Intel Realsense)

I've been working a lot with depth cameras previously, also for background segmentation (see [SurfaceStreams](https://github.com/floe/surface-streams)), so I just grabbed a leftover RealSense camera from the lab and gave it a shot. However, the depth data in a cluttered office environment is quite noisy, and no matter how I tweaked the camera settings, it could not produce any depth data for my hair...? I looked like a medieval monk who had the top of his head chopped off, so ... next.

### Attempt 1: OpenCV BackgroundSubtractor

See https://docs.opencv.org/3.4/d1/dc5/tutorial_background_subtraction.html for tutorial.
Should work OK for mostly static backgrounds and small moving objects, but does not work for a mostly static person in front of a static background. Next.

### Attempt 2: OpenCV Face Detector

See https://docs.opencv.org/3.4/db/d28/tutorial_cascade_classifier.html for tutorial.
Works okay-ish, but obviously only detects the face, and not the rest of the person. Also, only roughly matches an ellipse which is looking rater weird in the end. Next.

### Attempt 3: Deep learning!

I've heard good things about this deep learning stuff, so let's try that. I first had to find my way through a pile of frameworks (Keras, Tensorflow, PyTorch, etc.), but after I found a ready-made model for semantic segmentation based on Tensorflow Lite ([DeepLab v3+](https://tfhub.dev/tensorflow/lite-model/deeplabv3/1/default/1)), I settled on that.

I had a look at the corresponding [Python example](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/examples/python/label_image.py), [C++ example](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/examples/label_image), and [Android example](https://github.com/tensorflow/examples/tree/master/lite/examples/image_segmentation/android), and based on those, I first cobbled together a [Python demo](https://github.com/floe/backscrub/blob/master/deepseg.py). That was running at about 2.5 FPS, which is really excruciatingly slow, so I built a [C++ version](https://github.com/floe/backscrub/blob/master/deepseg.cc) which manages 10 FPS without too much hand optimization. Good enough.

I've also tested a TFLite-converted version of the [Body-Pix model](https://blog.tensorflow.org/2019/11/updated-bodypix-2.html), but the results haven't been much different to DeepLab for this use case.

More recently, Google has released a model specifically trained for [person segmentation that's used in Google Meet](https://ai.googleblog.com/2020/10/background-features-in-google-meet.html). This has way better performance than DeepLab, both in terms of speed and of accuracy, so this is now the default. It needs one custom op from the MediaPipe framework, but that was quite easy to integrate. Thanks to @jiangjianping for pointing this out in the [corresponding issue](https://github.com/floe/backscrub/issues/28).

## Replace Background

This is basically one line of code with OpenCV: `bg.copyTo(raw,mask);` Told you that's the easy part.

## Virtual Video Device

I'm using [v4l2loopback](https://github.com/umlaeute/v4l2loopback) to pipe the data from my userspace tool into any software that can open a V4L2 device. This isn't too hard because of the nice examples, but there are some catches, most notably color space. It took quite some trial and error to find a common pixel format that's accepted by Firefox, Skype, and guvcview, and that is [YUYV](https://www.linuxtv.org/downloads/v4l-dvb-apis-old/V4L2-PIX-FMT-YUYV.html). Nicely enough, my webcam can output YUYV directly as raw data, so that does save me some colorspace conversions.

## End Result

The dataflow through the whole program is roughly as follows:

  - init
    - load background.png, convert to YUYV
    - initialize TFLite, register custom op
    - load Google Meet segmentation model
    - setup V4L2 Loopback device (w,h,YUYV)
  - loop
    - grab raw YUYV image from camera
    - extract portrait ROI in center
      - downscale ROI to 144 x 256 (*)
      - convert to RGB float32 (*)
      - run Google Meet segmentation model 
      - convert result to binary mask using softmax
      - denoise mask using erode/dilate
    - upscale mask to raw image size
    - copy background over raw image with mask (see above)
    - `write()` data to virtual video device

(*) these are required input parameters for this model

## Limitations/Extensions

As usual: pull requests welcome.

See [Issues](https://github.com/floe/backscrub/issues) and [Pull Requests](https://github.com/floe/backscrub/pulls) for currently discussed/in-progress extensions, and also check out the `experimental` branch.

## Fixed
  
  - The project name isn't catchy enough. Help me find a nice [backronym](https://en.wikipedia.org/wiki/Backronym).
  - Resolution is currently hardcoded to 640x480 (lowest common denominator).
  - Only works with Linux, because that's what I use.
  - Needs a webcam that can produce raw YUYV data (but extending to the common YUV420 format should be trivial)
  - Should probably do a erosion (+ dilation?) operation on the mask.
  - Background image size needs to match camera resolution (see issue #1).
  - CPU hog: maxes out two cores on my 2.7 GHz i5 machine for just VGA @ 10 FPS. Fixed via Google Meet segmentation model.
  - Uses stock Deeplab v3+ network. Maybe re-training with only "person" and "background" classes could improve performance? Fixed via Google Meet segmentation model.

## Other links

Firefox preferred formats: https://searchfox.org/mozilla-central/source/third_party/libwebrtc/webrtc/modules/video_capture/linux/video_capture_linux.cc#142-159

## Feeding obs-studio

[We have been notified](https://github.com/floe/backscrub/issues/105) that some snap packaged versions of `obs-studio` are unable to detect/use a virtual camera as provided by `backscrub`. Please check the details for workarounds if this applies to you.
