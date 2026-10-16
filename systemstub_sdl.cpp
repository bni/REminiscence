/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2019 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <SDL3/SDL.h>
#include <sys/time.h>
#include "scaler.h"
#include "screenshot.h"
#include "shader.h"
#include "systemstub.h"
#include "util.h"

static const int kDefaultOutputSampleRate = 22050;

static const char *kIconBmp = "icon.bmp";

static const int kJoystickIndex = 0;
static const int kJoystickCommitValue = 3200;

static const SDL_PixelFormat kPixelFormat = SDL_PIXELFORMAT_XRGB8888;

ScalerParameters ScalerParameters::defaults() {
	ScalerParameters params;
	params.type = kScalerTypeInternal;
	params.name[0] = 0;
	params.factor = _internalScaler.factorMin + (_internalScaler.factorMax - _internalScaler.factorMin) / 2;
	return params;
}

struct SystemStub_SDL : SystemStub {
	SDL_Window *_window;
	SDL_Renderer *_renderer;
	SDL_Texture *_texture;
	int _texW, _texH;
	SDL_FRect _texRect;
	SDL_Gamepad *_controller;
	const SDL_PixelFormatDetails *_fmtDetails;
	const char *_caption;
	uint32_t *_screenBuffer;
	bool _fullscreen;
	uint32_t _clearColor;
	uint32_t _rgbPalette[256];
	uint32_t _darkPalette[256];
	int _screenW, _screenH;
	SDL_Joystick *_joystick;
	SDL_JoystickID _joystickId;
	bool _fadeOnUpdateScreen;
	void (*_audioCbProc)(void *, int16_t *, int);
	void *_audioCbData;
	SDL_AudioStream *_audioStream;
	Shader *_shader;
	SDL_Texture *_offscreenTexture;
	SDL_FRect _outputRect;
	int _windowW, _windowH;
	int _outputSampleRate;

	virtual ~SystemStub_SDL() {}
	virtual void init(const char *title, int w, int h, bool fullscreen, int widescreenMode, bool maximized, const ScalerParameters *scalerParameters, int outputRate);
	virtual void destroy();
	virtual bool hasWidescreen() const;
	virtual void setScreenSize(int w, int h);
	virtual void setPalette(const uint8_t *pal, int n);
	virtual void getPalette(uint8_t *pal, int n);
	virtual void setPaletteEntry(int i, const Color *c);
	virtual void getPaletteEntry(int i, Color *c);
	virtual void setOverscanColor(int i);
	virtual void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch);
	virtual void copyRectRgb24(int x, int y, int w, int h, const uint8_t *rgb);
	virtual void zoomRect(int x, int y, int w, int h);
	virtual void copyWidescreenLeft(int w, int h, const uint8_t *buf);
	virtual void copyWidescreenRight(int w, int h, const uint8_t *buf);
	virtual void copyWidescreenMirror(int w, int h, const uint8_t *buf);
	virtual void copyWidescreenBlur(int w, int h, const uint8_t *buf);
	virtual void copyWidescreenCDi(int w, int h, const uint8_t *buf, const uint8_t *pal);
	virtual void clearWidescreen();
	virtual void enableWidescreen(bool enable);
	virtual void fadeScreen();
	virtual void updateScreen(int shakeOffset);
	virtual void processEvents();
	virtual void sleep(int duration);
	virtual uint32_t getTimeStamp();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32_t getOutputSampleRate();
	virtual void lockAudio();
	virtual void unlockAudio();

	void setPaletteColor(int color, int r, int g, int b);
	void processEvent(const SDL_Event &ev, bool &paused);
	void prepareGraphics();
	void cleanupGraphics();
	void changeGraphics(bool fullscreen, int scaleFactor);
	void setScaler(const ScalerParameters *parameters);
	void changeScaler(int scalerNum);
	void drawRect(int x, int y, int w, int h, uint8_t color);
};

SystemStub *SystemStub_SDL_create() {
	return new SystemStub_SDL();
}

void SystemStub_SDL::init(const char *title, int w, int h, bool fullscreen, int widescreenMode, bool maximized, const ScalerParameters *scalerParameters, int outputRate) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
	if (fullscreen) {
		SDL_HideCursor();
	}
	_caption = title;
	memset(&_pi, 0, sizeof(_pi));
	_window = 0;
	_renderer = 0;
	_texture = 0;
	_fmtDetails = SDL_GetPixelFormatDetails(kPixelFormat);
	_screenBuffer = 0;
	_fadeOnUpdateScreen = false;
	_fullscreen = fullscreen;
	_clearColor = SDL_MapRGB(_fmtDetails, NULL, 0, 0, 0);
	memset(_rgbPalette, 0, sizeof(_rgbPalette));
	memset(_darkPalette, 0, sizeof(_darkPalette));
	_screenW = _screenH = 0;
	_joystick = 0;
	_joystickId = 0;
	_controller = 0;
	_audioStream = 0;
	_shader = 0;
	_offscreenTexture = 0;
	_outputRect = {};
	_windowW = _windowH = 0;
	setScreenSize(w, h);
	int numJoysticks = 0;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&numJoysticks);
	if (joysticks && numJoysticks > 0) {
		SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
		SDL_JoystickID id = joysticks[kJoystickIndex];
		if (SDL_IsGamepad(id)) {
			_controller = SDL_OpenGamepad(id);
		}
		if (!_controller) {
			_joystick = SDL_OpenJoystick(id);
			_joystickId = id;
		}
		SDL_free(joysticks);
	}
	_outputSampleRate = !outputRate ? kDefaultOutputSampleRate : outputRate;
}

void SystemStub_SDL::destroy() {
	cleanupGraphics();
	if (_screenBuffer) {
		free(_screenBuffer);
		_screenBuffer = 0;
	}
	_fmtDetails = 0;
	if (_controller) {
		SDL_CloseGamepad(_controller);
		_controller = 0;
	}
	if (_joystick) {
		SDL_CloseJoystick(_joystick);
		_joystick = 0;
	}
	SDL_Quit();
}

bool SystemStub_SDL::hasWidescreen() const {
	return false;
}

void SystemStub_SDL::setScreenSize(int w, int h) {
	if (_screenW == w && _screenH == h) {
		return;
	}
	cleanupGraphics();
	if (_screenBuffer) {
		free(_screenBuffer);
		_screenBuffer = 0;
	}
	const int screenBufferSize = w * h;
	_screenBuffer = (uint32_t *)calloc(screenBufferSize, sizeof(uint32_t));
	if (!_screenBuffer) {
		error("SystemStub_SDL::setScreenSize() Unable to allocate offscreen buffer, w=%d, h=%d", w, h);
	}
	_screenW = w;
	_screenH = h;
	prepareGraphics();
}

void SystemStub_SDL::setPaletteColor(int color, int r, int g, int b) {
	_rgbPalette[color] = SDL_MapRGB(_fmtDetails, NULL, r, g, b);
	_darkPalette[color] = SDL_MapRGB(_fmtDetails, NULL, r / 4, g / 4, b / 4);
}

void SystemStub_SDL::setPalette(const uint8_t *pal, int n) {
	assert(n <= 256);
	for (int i = 0; i < n; ++i) {
		setPaletteColor(i, pal[0], pal[1], pal[2]);
		pal += 3;
	}
}

void SystemStub_SDL::getPalette(uint8_t *pal, int n) {
	assert(n <= 256);
	for (int i = 0; i < n; ++i) {
		SDL_GetRGB(_rgbPalette[i], _fmtDetails, NULL, &pal[0], &pal[1], &pal[2]);
		pal += 3;
	}
}

void SystemStub_SDL::setPaletteEntry(int i, const Color *c) {
	setPaletteColor(i, c->r, c->g, c->b);
}

void SystemStub_SDL::getPaletteEntry(int i, Color *c) {
	SDL_GetRGB(_rgbPalette[i], _fmtDetails, NULL, &c->r, &c->g, &c->b);
}

void SystemStub_SDL::setOverscanColor(int i) {
	if (0) {
		_clearColor = _rgbPalette[i];
	}
}

void SystemStub_SDL::copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch) {
	if (x < 0) {
		x = 0;
	} else if (x >= _screenW) {
		return;
	}
	if (y < 0) {
		y = 0;
	} else if (y >= _screenH) {
		return;
	}
	if (x + w > _screenW) {
		w = _screenW - x;
	}
	if (y + h > _screenH) {
		h = _screenH - y;
	}

	uint32_t *p = _screenBuffer + y * _screenW + x;
	buf += y * pitch + x;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			p[i] = _rgbPalette[buf[i]];
		}
		p += _screenW;
		buf += pitch;
	}

	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
		drawRect(x, y, w, h, 0xE7);
	}
}

void SystemStub_SDL::copyRectRgb24(int x, int y, int w, int h, const uint8_t *rgb) {
	assert(x >= 0 && x + w <= _screenW && y >= 0 && y + h <= _screenH);
	uint32_t *p = _screenBuffer + y * _screenW + x;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			p[i] = SDL_MapRGB(_fmtDetails, NULL, rgb[0], rgb[1], rgb[2]); rgb += 3;
		}
		p += _screenW;
	}

	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
		drawRect(x, y, w, h, 0xE7);
	}
}

void SystemStub_SDL::zoomRect(int x, int y, int w, int h) {
	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
		drawRect(x, y, w, h, 0xE7);
	}
	_texRect.x = (float)(x * _texW / _screenW);
	_texRect.y = (float)(y * _texH / _screenH);
	_texRect.w = (float)(w * _texW / _screenW);
	_texRect.h = (float)(h * _texH / _screenH);
}

static void clearTexture(SDL_Texture *texture, int h, uint32_t color) {
	void *dst = 0;
	int pitch = 0;
	if (SDL_LockTexture(texture, 0, &dst, &pitch)) {
		assert((pitch & 3) == 0);
		for (uint32_t i = 0; i < h * pitch / sizeof(uint32_t); ++i) {
			((uint32_t *)dst)[i] = color;
		}
		SDL_UnlockTexture(texture);
	}
}

void SystemStub_SDL::copyWidescreenLeft(int w, int h, const uint8_t *buf) {
}

void SystemStub_SDL::copyWidescreenRight(int w, int h, const uint8_t *buf) {
}

void SystemStub_SDL::copyWidescreenMirror(int w, int h, const uint8_t *buf) {
}

void SystemStub_SDL::copyWidescreenBlur(int w, int h, const uint8_t *buf) {
}

void SystemStub_SDL::copyWidescreenCDi(int w, int h, const uint8_t *buf, const uint8_t *pal) {
}

void SystemStub_SDL::clearWidescreen() {
}

void SystemStub_SDL::enableWidescreen(bool enable) {
}

void SystemStub_SDL::fadeScreen() {
	_fadeOnUpdateScreen = true;
}

void SystemStub_SDL::updateScreen(int shakeOffset) {
	SDL_UpdateTexture(_texture, 0, _screenBuffer, _screenW * sizeof(uint32_t));

	if (_fullscreen && _offscreenTexture) {
		// Fullscreen mode with CRT shader
		// Note: fade effect not supported with GPU renderer, just show final frame
		if (_fadeOnUpdateScreen) {
			_fadeOnUpdateScreen = false;
		}

		// Render to offscreen texture first
		SDL_SetRenderTarget(_renderer, _offscreenTexture);
		SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
		SDL_RenderClear(_renderer);

		SDL_FRect offscreenRect;
		offscreenRect.x = 0;
		offscreenRect.y = (float)shakeOffset;
		offscreenRect.w = (float)_texW;
		offscreenRect.h = (float)_texH;
		SDL_RenderTexture(_renderer, _texture, &_texRect, &offscreenRect);

		// Render to screen with shader
		SDL_SetRenderTarget(_renderer, nullptr);
		SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
		SDL_RenderClear(_renderer);

		if (_shader && _shader->isAvailable()) {
			_shader->setUniforms(_outputRect, _windowW, _windowH);
			_shader->enable();
			SDL_RenderTexture(_renderer, _offscreenTexture, nullptr, &_outputRect);
			_shader->disable();
		} else {
			SDL_RenderTexture(_renderer, _offscreenTexture, nullptr, &_outputRect);
		}

		SDL_RenderPresent(_renderer);
	} else {
		// Windowed mode - 4x integer scaling, no shader
		SDL_RenderClear(_renderer);

		if (_fadeOnUpdateScreen) {
			SDL_FRect dest;
			dest.x = 0;
			dest.y = 0;
			dest.w = (float)_windowW;
			dest.h = (float)_windowH;
			for (int i = 1; i <= 16; ++i) {
				SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
				SDL_RenderClear(_renderer);
				SDL_SetTextureAlphaModFloat(_texture, i / 16.0f);
				SDL_RenderTexture(_renderer, _texture, NULL, &dest);
				SDL_RenderPresent(_renderer);
				SDL_Delay(30);
			}
			SDL_SetTextureAlphaModFloat(_texture, 1.0f);
			_fadeOnUpdateScreen = false;
			return;
		}
		SDL_FRect r;
		r.x = 0;
		r.y = (float)(shakeOffset * 4);
		r.w = (float)_windowW;
		r.h = (float)_windowH;

		SDL_RenderTexture(_renderer, _texture, &_texRect, &r);

		SDL_RenderPresent(_renderer);
	}
	_texRect.x = 0;
	_texRect.y = 0;
	_texRect.w = (float)_texW;
	_texRect.h = (float)_texH;
}

void SystemStub_SDL::processEvents() {
	bool paused = false;
	while (true) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			processEvent(ev, paused);
			if (_pi.quit) {
				return;
			}
		}
		if (!paused) {
			break;
		}
		SDL_Delay(100);
	}
}

// only used for the protection codes and level passwords
static void setAsciiChar(PlayerInput &pi, SDL_Keycode sym, SDL_Scancode scancode) {
	if (sym >= SDLK_0 && sym <= SDLK_9) {
		pi.lastChar = '0' + sym - SDLK_0;
	} else if (sym >= SDLK_A && sym <= SDLK_Z) {
		pi.lastChar = 'A' + sym - SDLK_A;
	} else if (scancode == SDL_SCANCODE_0) {
		pi.lastChar = '0';
	} else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
		pi.lastChar = '1' + scancode - SDL_SCANCODE_1;
	} else if (sym == SDLK_SPACE || sym == SDLK_KP_SPACE) {
		pi.lastChar = ' ';
	} else {
		pi.lastChar = 0;
	}
}

void SystemStub_SDL::processEvent(const SDL_Event &ev, bool &paused) {
	switch (ev.type) {
	case SDL_EVENT_QUIT:
		_pi.quit = true;
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		paused = false;
		if (_audioStream) {
			SDL_ResumeAudioStreamDevice(_audioStream);
		}
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		paused = true;
		if (_audioStream) {
			SDL_PauseAudioStreamDevice(_audioStream);
		}
		break;
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
		if (_joystick) {
			_pi.dirMask = 0;
			if (ev.jhat.value & SDL_HAT_UP) {
				_pi.dirMask |= PlayerInput::DIR_UP;
			}
			if (ev.jhat.value & SDL_HAT_DOWN) {
				_pi.dirMask |= PlayerInput::DIR_DOWN;
			}
			if (ev.jhat.value & SDL_HAT_LEFT) {
				_pi.dirMask |= PlayerInput::DIR_LEFT;
			}
			if (ev.jhat.value & SDL_HAT_RIGHT) {
				_pi.dirMask |= PlayerInput::DIR_RIGHT;
			}
		}
		break;
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		if (_joystick) {
			/*switch (ev.jaxis.axis) {
			case 0:
				_pi.dirMask &= ~(PlayerInput::DIR_RIGHT | PlayerInput::DIR_LEFT);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				}
				break;
			case 1:
				_pi.dirMask &= ~(PlayerInput::DIR_UP | PlayerInput::DIR_DOWN);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				}
				break;
			}*/
		}
		break;
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
		if (_joystick) {
			const bool pressed = ev.jbutton.down;
			switch (ev.jbutton.button) {
			case 0:
				_pi.space = pressed;
				break;
			case 1:
				_pi.shift = pressed;
				break;
			case 2:
				_pi.enter = pressed;
				break;
			case 3:
				_pi.backspace = pressed;
				break;
			}
		}
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (_controller) {
			/*switch (ev.gaxis.axis) {
			case SDL_GAMEPAD_AXIS_LEFTX:
			case SDL_GAMEPAD_AXIS_RIGHTX:
				if (ev.gaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				if (ev.gaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			case SDL_GAMEPAD_AXIS_LEFTY:
			case SDL_GAMEPAD_AXIS_RIGHTY:
				if (ev.gaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				if (ev.gaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			}*/
		}
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (_controller) {
			const bool pressed = ev.gbutton.down;
			switch (ev.gbutton.button) {
			case SDL_GAMEPAD_BUTTON_SOUTH:
				_pi.enter = pressed;
				break;
			case SDL_GAMEPAD_BUTTON_EAST:
				_pi.space = pressed;
				break;
			case SDL_GAMEPAD_BUTTON_WEST:
				_pi.shift = pressed;
				break;
			case SDL_GAMEPAD_BUTTON_NORTH:
				_pi.backspace = pressed;
				break;
			case SDL_GAMEPAD_BUTTON_BACK:
			case SDL_GAMEPAD_BUTTON_START:
				_pi.escape = pressed;
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_UP:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			}
		}
		break;
	case SDL_EVENT_KEY_UP:
		if (ev.key.mod & SDL_KMOD_ALT) {
			switch (ev.key.key) {
			case SDLK_S: {
					struct timeval tv;
					gettimeofday(&tv, 0);
					const long timestamp = tv.tv_sec * 1000 + tv.tv_usec / 1000;
					char name[64];
					snprintf(name, sizeof(name), "screenshot-%ld.bmp", timestamp);
					saveBMP(name, (const uint8_t *)_screenBuffer, 0, _screenW, _screenH);
					info("Written '%s'", name);
				}
				break;
			case SDLK_X:
				_pi.quit = true;
				break;
			}
			break;
		} else if (ev.key.mod & SDL_KMOD_CTRL) {
			switch (ev.key.key) {
			case SDLK_F:
				_pi.dbgMask ^= PlayerInput::DF_FASTMODE;
				break;
			case SDLK_B:
				_pi.dbgMask ^= PlayerInput::DF_DBLOCKS;
				break;
			case SDLK_I:
				_pi.dbgMask ^= PlayerInput::DF_SETLIFE;
				break;
			case SDLK_S:
				_pi.save = true;
				break;
			case SDLK_L:
				_pi.load = true;
				break;
			case SDLK_R:
				_pi.rewind = true;
				break;
			case SDLK_G:
				_pi.dbgMask ^= PlayerInput::DF_AUTOZOOM;
				info("Auto zoom %s", (_pi.dbgMask & PlayerInput::DF_AUTOZOOM) ? "enabled" : "disabled");
				break;
			case SDLK_KP_PLUS:
			case SDLK_PAGEUP:
				_pi.stateSlot = 1;
				break;
			case SDLK_KP_MINUS:
			case SDLK_PAGEDOWN:
				_pi.stateSlot = -1;
				break;
			}
			break;
		}
		setAsciiChar(_pi, ev.key.key, ev.key.scancode);
		switch (ev.key.key) {
		case SDLK_LEFT:
		case SDLK_KP_4:
			_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
		case SDLK_KP_6:
			_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
		case SDLK_KP_8:
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
		case SDLK_KP_2:
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			break;
		case SDLK_SPACE:
			_pi.space = false;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = false;
			break;
		case SDLK_RETURN:
			_pi.enter = false;
			break;
		case SDLK_ESCAPE:
			_pi.escape = false;
			break;
		case SDLK_F1:
		case SDLK_F2:
		case SDLK_F3:
		case SDLK_F4:
		case SDLK_F5:
		case SDLK_F6:
		case SDLK_F7:
		case SDLK_F8:
			changeScaler(ev.key.key - SDLK_F1);
			break;
		default:
			break;
		}
		break;
	case SDL_EVENT_KEY_DOWN:
		if (ev.key.mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL)) {
			break;
		}
		switch (ev.key.key) {
		case SDLK_LEFT:
		case SDLK_KP_4:
			_pi.dirMask |= PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
		case SDLK_KP_6:
			_pi.dirMask |= PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
		case SDLK_KP_8:
			_pi.dirMask |= PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
		case SDLK_KP_2:
			_pi.dirMask |= PlayerInput::DIR_DOWN;
			break;
		case SDLK_BACKSPACE:
		case SDLK_TAB:
			_pi.backspace = true;
			break;
		case SDLK_SPACE:
			_pi.space = true;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = true;
			break;
		case SDLK_RETURN:
			_pi.enter = true;
			break;
		case SDLK_ESCAPE:
			_pi.escape = true;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

void SystemStub_SDL::sleep(int duration) {
	SDL_Delay(duration);
}

uint32_t SystemStub_SDL::getTimeStamp() {
	return SDL_GetTicks();
}

static void SDLCALL audioStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
	SystemStub_SDL *stub = (SystemStub_SDL *)userdata;
	if (additional_amount > 0) {
		uint8_t *buf = (uint8_t *)SDL_malloc(additional_amount);
		if (buf) {
			memset(buf, 0, additional_amount);
			assert((additional_amount & 3) == 0);
			stub->_audioCbProc(stub->_audioCbData, (int16_t *)buf, additional_amount / (sizeof(int16_t) * 2));
			SDL_PutAudioStreamData(stream, buf, additional_amount);
			SDL_free(buf);
		}
	}
}

void SystemStub_SDL::startAudio(AudioCallback callback, void *param) {
	SDL_AudioSpec spec;
	spec.freq = _outputSampleRate;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	_audioCbProc = callback;
	_audioCbData = param;
	_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioStreamCallback, this);
	if (_audioStream) {
		SDL_ResumeAudioStreamDevice(_audioStream);
	} else {
		error("SystemStub_SDL::startAudio() Unable to open sound device");
	}
}

void SystemStub_SDL::stopAudio() {
	if (_audioStream) {
		SDL_DestroyAudioStream(_audioStream);
		_audioStream = 0;
	}
}

uint32_t SystemStub_SDL::getOutputSampleRate() {
	return _outputSampleRate;
}

void SystemStub_SDL::lockAudio() {
	if (_audioStream) {
		SDL_LockAudioStream(_audioStream);
	}
}

void SystemStub_SDL::unlockAudio() {
	if (_audioStream) {
		SDL_UnlockAudioStream(_audioStream);
	}
}

void SystemStub_SDL::prepareGraphics() {
	_texW = _screenW;
	_texH = _screenH;
	_texRect.x = 0;
	_texRect.y = 0;
	_texRect.w = (float)_texW;
	_texRect.h = (float)_texH;
	SDL_WindowFlags flags = 0;
	if (_fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIDDEN;
		SDL_DisplayID display = SDL_GetPrimaryDisplay();
		const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
		if (mode) {
			_windowW = mode->w;
			_windowH = mode->h;
		} else {
			_windowW = _screenW;
			_windowH = _screenH;
		}
	} else {
		// Windowed mode: 4x integer scaling
		_windowW = _screenW * 4;
		_windowH = _screenH * 4;
	}
	_window = SDL_CreateWindow(_caption, _windowW, _windowH, flags);
	if (_fullscreen) {
		SDL_SetWindowPosition(_window, 0, 0);
	} else {
		SDL_SetWindowPosition(_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
	SDL_Surface *icon = SDL_LoadBMP(kIconBmp);
	if (icon) {
		SDL_SetWindowIcon(_window, icon);
		SDL_DestroySurface(icon);
	}

	// Create renderer - use GPU renderer for fullscreen CRT shader
	if (_fullscreen) {
		if (SDL_GPUDevice *device = SDL_CreateGPUDevice(
			SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV,
			false,
			nullptr)) {
			_renderer = SDL_CreateGPURenderer(device, _window);
			if (!_renderer) {
				warning("GPU Renderer could not be created, falling back to standard renderer");
				SDL_DestroyGPUDevice(device);
			}
		}
		if (!_renderer) {
			_renderer = SDL_CreateRenderer(_window, NULL);
		}
	} else {
		_renderer = SDL_CreateRenderer(_window, NULL);
	}

	_texture = SDL_CreateTexture(_renderer, kPixelFormat, SDL_TEXTUREACCESS_STREAMING, _texW, _texH);
	SDL_SetTextureScaleMode(_texture, SDL_SCALEMODE_NEAREST);

	// Create offscreen texture and shader for fullscreen mode
	if (_fullscreen) {
		_offscreenTexture = SDL_CreateTexture(
			_renderer,
			kPixelFormat,
			SDL_TEXTUREACCESS_TARGET,
			_texW, _texH
		);

		// Initialize shader
		_shader = new Shader();
		if (!_shader->init(_renderer)) {
			debug(DBG_INFO, "Shader not available, using integer scaling");
		}

		if (_shader->isAvailable()) {
			// Shader available: aspect-ratio preserving output rectangle
			const float gameAspect = (float)_texW / (float)_texH;
			const float screenAspect = (float)_windowW / (float)_windowH;
			if (screenAspect > gameAspect) {
				_outputRect.h = (float)_windowH;
				_outputRect.w = _outputRect.h * gameAspect;
				_outputRect.x = ((float)_windowW - _outputRect.w) / 2.0f;
				_outputRect.y = 0.0f;
			} else {
				_outputRect.w = (float)_windowW;
				_outputRect.h = _outputRect.w / gameAspect;
				_outputRect.x = 0.0f;
				_outputRect.y = ((float)_windowH - _outputRect.h) / 2.0f;
			}
		} else {
			// Shader not available: pixel-perfect integer scaling
			const int scaleX = _windowW / _texW;
			const int scaleY = _windowH / _texH;
			const int scale = (scaleX < scaleY) ? scaleX : scaleY;
			_outputRect.w = (float)(_texW * scale);
			_outputRect.h = (float)(_texH * scale);
			_outputRect.x = ((float)_windowW - _outputRect.w) / 2.0f;
			_outputRect.y = ((float)_windowH - _outputRect.h) / 2.0f;
			SDL_SetTextureScaleMode(_offscreenTexture, SDL_SCALEMODE_NEAREST);
		}

		// Clear screen to black and show window
		SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
		SDL_RenderClear(_renderer);
		SDL_RenderPresent(_renderer);
		SDL_ShowWindow(_window);
	}
}

void SystemStub_SDL::cleanupGraphics() {
	if (_shader) {
		delete _shader;
		_shader = 0;
	}
	if (_offscreenTexture) {
		SDL_DestroyTexture(_offscreenTexture);
		_offscreenTexture = 0;
	}
	if (_texture) {
		SDL_DestroyTexture(_texture);
		_texture = 0;
	}
	if (_renderer) {
		SDL_DestroyRenderer(_renderer);
		_renderer = 0;
	}
	if (_window) {
		SDL_DestroyWindow(_window);
		_window = 0;
	}
}

void SystemStub_SDL::changeGraphics(bool fullscreen, int scaleFactor) {
}

void SystemStub_SDL::setScaler(const ScalerParameters *parameters) {
}

void SystemStub_SDL::changeScaler(int scalerNum) {
}

void SystemStub_SDL::drawRect(int x, int y, int w, int h, uint8_t color) {
	const int x1 = x;
	const int y1 = y;
	const int x2 = x + w - 1;
	const int y2 = y + h - 1;
	assert(x1 >= 0 && x2 < _screenW && y1 >= 0 && y2 < _screenH);
	for (int i = x1; i <= x2; ++i) {
		*(_screenBuffer + y1 * _screenW + i) = *(_screenBuffer + y2 * _screenW + i) = _rgbPalette[color];
	}
	for (int j = y1; j <= y2; ++j) {
		*(_screenBuffer + j * _screenW + x1) = *(_screenBuffer + j * _screenW + x2) = _rgbPalette[color];
	}
}
