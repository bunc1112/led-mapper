#pragma once
#include <chrono>
#include <vector>
#include <FFGLSDK.h>
#include "Envelope.h"

// PUMP - makes a layer pump on the beat (or on the real kick drum from Resolume's audio),
// keying its dark parts so the layer underneath becomes the background.
class Pump : public CFFGLPlugin
{
public:
	Pump();
	~Pump() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;
	void SetSampleRate( unsigned int sampleRate ) override;

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	pump::Envelope envelope;
	pump::Settings settings;
	std::chrono::steady_clock::time_point startTime;
	float lastBarPhase = -1.0f;
	bool hostBeat      = false;
	float sampleRateHz = 44100.0f;
	std::vector< float > fft;
	float values[ 40 ] = {};
	float envDisplay = 0.0f;
	char displayBuf[ 32 ];
};
