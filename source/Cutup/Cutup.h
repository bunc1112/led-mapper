#pragma once
#include <chrono>
#include <string>
#include <FFGLSDK.h>
#include "Engine.h"

// CUTUP - beat-synced recap/montage effect: words pop in letter by letter with the video inside them,
// cards cascade, zoom and scatter, the frame irises, blinds and grids open, all cut on the beat
// or on the "Next" button. Output can be video-in-shapes, holes (layer below shows through) or a matte.
class Cutup : public CFFGLPlugin
{
public:
	Cutup();
	~Cutup() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	GLuint atlasTex = 0;
	GLuint primTex  = 0;

	cutup::Engine engine;
	cutup::Settings settings;
	std::chrono::steady_clock::time_point startTime;
	float lastBarPhase = -1.0f;
	bool hostBeat      = false;

	// raw parameter values
	float values[ 64 ] = {};
	float hue = 0.995f, sat = 0.9f, bri = 0.55f;
	int output = 0, background = 0, division = 2;
};
