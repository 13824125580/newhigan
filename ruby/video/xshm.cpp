//XShm driver for Xorg

//Note that on composited displays, the alpha bits will allow translucency underneath the active window
//As this is not a feature of ruby, this driver must always set the alpha bits on clear() and refresh()

//Linear interpolation is only applied horizontally for performance reasons, although Nearest is still much faster

#include <sys/shm.h>
#include <X11/extensions/XShm.h>

struct VideoXShm : VideoDriver {
  VideoXShm& self = *this;
  VideoXShm(Video& super) : VideoDriver(super) { construct(); }
  ~VideoXShm() { destruct(); }

  auto create() -> bool override {
    return initialize();
  }

  auto driver() -> string override { return "XShm"; }
  auto ready() -> bool override { return _ready; }

  auto hasContext() -> bool override { return true; }
  auto hasShader() -> bool override { return true; }

  auto hasFormats() -> vector<string> override { return {"RGB24"}; }

  auto setContext(uintptr context) -> bool override { return initialize(); }
  auto setShader(string shader) -> bool override { return true; }

  auto configure(uint width, uint height, double inputFrequency, double outputFrequency) -> bool override {
    if(width == _outputWidth && height == _outputHeight) return true;

    _outputWidth = width;
    _outputHeight = height;
    XResizeWindow(_display, _window, _outputWidth, _outputHeight);
    free();

    _shmInfo.shmid = shmget(IPC_PRIVATE, _outputWidth * _outputHeight * sizeof(uint32_t), IPC_CREAT | 0777);
    if(_shmInfo.shmid < 0) return false;

    _shmInfo.shmaddr = (char*)shmat(_shmInfo.shmid, 0, 0);
    _shmInfo.readOnly = False;
    XShmAttach(_display, &_shmInfo);
    _outputBuffer = (uint32_t*)_shmInfo.shmaddr;
    _image = XShmCreateImage(_display, _visual, _depth, ZPixmap, _shmInfo.shmaddr, &_shmInfo, _outputWidth, _outputHeight);
    return (bool)_image;
  }

  auto clear() -> void override {
    auto dp = _inputBuffer;
    uint length = _inputWidth * _inputHeight;
    while(length--) *dp++ = 255u << 24;
    output();
  }

  auto acquire(uint32_t*& data, uint& pitch, uint width, uint height) -> bool override {
    if(!_inputBuffer || _inputWidth != width || _inputHeight != height) {
      if(_inputBuffer) delete[] _inputBuffer;
      _inputWidth = width;
      _inputHeight = height;
      _inputBuffer = new uint32_t[width * height + 16];  //+16 is padding for linear interpolation
    }

    data = _inputBuffer;
    pitch = _inputWidth * sizeof(uint32_t);
    return true;
  }

  auto release() -> void override {
  }

  auto output() -> void override {
    float xratio = (float)_inputWidth / (float)_outputWidth;
    float yratio = (float)_inputHeight / (float)_outputHeight;

    #pragma omp parallel for
    for(uint y = 0; y < _outputHeight; y++) {
      float ystep = y * yratio;
      float xstep = 0;

      uint32_t* sp = _inputBuffer + (uint)ystep * _inputWidth;
      uint32_t* dp = _outputBuffer + y * _outputWidth;

      if(self.shader != "Blur") {
        for(uint x = 0; x < _outputWidth; x++) {
          *dp++ = 255u << 24 | sp[(uint)xstep];
          xstep += xratio;
        }
      } else {
        for(uint x = 0; x < _outputWidth; x++) {
          *dp++ = 255u << 24 | interpolate(xstep - (uint)xstep, sp[(uint)xstep], sp[(uint)xstep + 1]);
          xstep += xratio;
        }
      }
    }

    GC gc = XCreateGC(_display, _window, 0, 0);
    XShmPutImage(_display, _window, gc, _image, 0, 0, 0, 0, _outputWidth, _outputHeight, False);
    XFreeGC(_display, gc);
    XFlush(_display);
  }

  auto poll() -> void override {
    while(XPending(_display)) {
      XEvent event;
      XNextEvent(_display, &event);
      if(event.type == Expose) {
        XWindowAttributes attributes{};
        XGetWindowAttributes(_display, _window, &attributes);
        super.doUpdate(attributes.width, attributes.height);
      }
    }
  }

private:
  auto construct() -> void {
    _display = XOpenDisplay(nullptr);
    _screen = DefaultScreen(_display);
    XSetErrorHandler(errorHandler);
  }

  auto destruct() -> void {
    XCloseDisplay(_display);
  }

  auto initialize() -> bool {
    terminate();
    if(!self.context) return false;

    XWindowAttributes getAttributes{};
    XGetWindowAttributes(_display, (Window)self.context, &getAttributes);
    _depth = getAttributes.depth;
    _visual = getAttributes.visual;
    //driver only supports 32-bit pixels
    //note that even on 15-bit and 16-bit displays, the window visual's depth should be 32
    if(_depth < 24 || _depth > 32) {
      free();
      return false;
    }

    XSetWindowAttributes setAttributes = {};
    setAttributes.border_pixel = 0;
    _window = XCreateWindow(_display, (Window)self.context,
      0, 0, 256, 256, 0,
      getAttributes.depth, InputOutput, getAttributes.visual,
      CWBorderPixel, &setAttributes
    );
    XSelectInput(_display, _window, ExposureMask);
    XSetWindowBackground(_display, _window, 0);
    XMapWindow(_display, _window);
    XFlush(_display);

    while(XPending(_display)) {
      XEvent event;
      XNextEvent(_display, &event);
    }

    return _ready = true;
  }

  auto terminate() -> void {
    free();
  }

  auto free() -> void {
    if(_outputBuffer) {
      _outputBuffer = nullptr;
      XShmDetach(_display, &_shmInfo);
      XDestroyImage(_image);
      shmdt(_shmInfo.shmaddr);
      shmctl(_shmInfo.shmid, IPC_RMID, 0);
    }
  }

  alwaysinline auto interpolate(float mu, uint32_t a, uint32_t b) -> uint32_t {
    uint8_t ar = a >> 16, ag = a >> 8, ab = a >> 0;
    uint8_t br = b >> 16, bg = b >> 8, bb = b >> 0;
    uint8_t cr = ar * (1.0 - mu) + br * mu;
    uint8_t cg = ag * (1.0 - mu) + bg * mu;
    uint8_t cb = ab * (1.0 - mu) + bb * mu;
    return cr << 16 | cg << 8 | cb << 0;
  }

  static auto errorHandler(Display* display, XErrorEvent* event) -> int {
    //catch occasional BadAccess errors during window resize events
    //currently, I'm unsure of the cause, but they're certainly not fatal
    return 0;
  }

  bool _ready = false;

  uint32_t* _inputBuffer = nullptr;
  uint _inputWidth = 0;
  uint _inputHeight = 0;

  Display* _display = nullptr;
  int _screen = 0;
  int _depth = 0;
  Visual* _visual = nullptr;
  Window _window = 0;

  XShmSegmentInfo _shmInfo;
  XImage* _image = nullptr;

  uint32_t* _outputBuffer = nullptr;
  uint _outputWidth = 0;
  uint _outputHeight = 0;
};
