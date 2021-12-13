#pragma once

#ifndef __FourTapDelay__
#define __FourTapDelay__

#include "fxobjects.h"
#include "superlfo.h"
/**
\struct FourTapDelayParameters
\ingroup FX-Objects
\brief
Custom parameter structure for the FourTapDelay object.

\author <Your Name> <http://www.yourwebsite.com>
\remark <Put any remarks or notes here>
\version Revision : 1.0
\date Date : 2019 / 01 / 31
*/
struct FourTapDelayParameters
{
	FourTapDelayParameters() {}

	/** all FXObjects parameter objects require overloaded= operator so remember to add new entries if you add new variables. */
	FourTapDelayParameters& operator=(const FourTapDelayParameters& params)	// need this override for collections to work
	{
		// --- it is possible to try to make the object equal to itself
		//     e.g. thisObject = thisObject; so this code catches that
		//     trivial case and just returns this object
		if (this == &params)
			return *this;

		// --- copy from params (argument) INTO our variables
		feedback_Pct = params.feedback_Pct;
		blend = params.blend;

		modeSelectorValue = params.modeSelectorValue;
		delayTime_long = params.delayTime_long;
		delayTime_short = params.delayTime_short;
		modDepth_Pct = params.modDepth_Pct;
		modRate_Hz = params.modRate_Hz;
		modType = params.modType;
		enableMod = params.enableMod;
		enableSidechain = params.enableSidechain;

		// --- MUST be last
		return *this;
	}

	// --- individual parameters
	
	double feedback_Pct = 0.0;
	double blend = 0.0;
	unsigned int modeSelectorValue;
	double delayTime_short = 0.0;
	double delayTime_long = 0.0;

	double modDepth_Pct = 0.0;
	double modRate_Hz = 0.0;
	int modType = 0;
	bool enableMod = false;
	bool enableSidechain = false;
};


/**
\class FourTapDelay
\ingroup FX-Objects
\brief
The FourTapDelay object implements ....

Audio I/O:
- Processes mono input to mono output.
- *** Optionally, process frame *** Modify this according to your object functionality

Control I/F:
- Use FourTapDelayParameters structure to get/set object params.

\author <Your Name> <http://www.yourwebsite.com>
\remark <Put any remarks or notes here>
\version Revision : 1.0
\date Date : 2019 / 01 / 31
*/
class FourTapDelay : public IAudioSignalProcessor
{
public:
	FourTapDelay(void) {}	/* C-TOR */
	~FourTapDelay(void) {}	/* D-TOR */

public:
	/** reset members to initialized state */
	virtual bool reset(double _sampleRate)
	{
		if (sampleRate == _sampleRate)
		{
			// --- just flush buffer and return
			delayBuffer.flushBuffer();
			return true;
		}
		createDelayBuffers(_sampleRate, bufferLength_mSec);
		sampleRate = _sampleRate;

		modDelay.reset(_sampleRate);
		modDelay.createDelayBuffers(_sampleRate, 100);

		lfo.reset(_sampleRate);
		SuperLFOParameters params;
		params.waveform = LFOWaveform::kTriangle;
		lfo.setParameters(params);

		AudioDetectorParameters adParams;
		adParams.attackTime_mSec = 1.0;
		adParams.releaseTime_mSec = 500.0;
		adParams.detectMode = TLD_AUDIO_DETECT_MODE_RMS;
		adParams.detect_dB = true;
		adParams.clampToUnityMax = false;
		detector.setParameters(adParams);

		sidechainInputSample = 0.0;

		return true;
	}

	/** process MONO input */
	/**
	\param xn input
	\return the processed sample
	*/
	virtual double processAudioSample(double xn)
	{
		double delayLines[4];
		double weightedFeedback_Pct[4];
		double weightedFeebackValues[4] = { 0.05, 0.1, 0.25, 0.6 };
		double weightedFeedbackOutput = 0.0;
		double yn = 0.0;
		double sc_depth = 0.0;

		if (parameters.enableSidechain) {
			detector.enableAuxInput(true);
			double sc_xn = detector.processAuxInputAudioSample(xn);
			double detect_dB = detector.processAudioSample(sc_xn);
			double detectValue = pow(10.0, detect_dB / 20.0);

			sc_depth = doUnipolarModulationFromMin(detectValue, 0.2, 1.0);
		}

		if ((parameters.enableMod && parameters.modeSelectorValue == 1) || (parameters.enableMod && parameters.modeSelectorValue == 2)) {
			double depth = parameters.modDepth_Pct / 200.0; 
			if (parameters.modType == 0) {
				depth = depth * 2.0;
			}

			if (parameters.enableSidechain) {
				depth = sc_depth;
			}
			double modMin = minDelay_mSec[parameters.modeSelectorValue - 1][parameters.modType];
			double modMax = modMin + modDepth_mSec[parameters.modeSelectorValue - 1][parameters.modType];

			double lfoOutput = lfo.renderModulatorOutput().normalOutput;
			AudioDelayParameters params = modDelay.getParameters();

			if (parameters.modType == 0) {
				params.leftDelay_mSec = doUnipolarModulationFromMin(bipolarToUnipolar(lfoOutput * depth), modMin, modMax);
			
			} else {
				params.leftDelay_mSec = doBipolarModulation(lfoOutput * depth, modMin, modMax);
			}
			double delay = delayBuffer.readBuffer(delayInSamples[0]);

			params.dryLevel_dB = modDry_dB[parameters.modType];
			params.wetLevel_dB = modWet_dB[parameters.modType];
			params.feedback_Pct = parameters.feedback_Pct;
			if (parameters.modType != 0) {
				params.feedback_Pct = 0.0;
			}
			modDelay.setParameters(params);
			return modDelay.processAudioSample(xn);

		} else {
			for (int i = 0; i < 4; i++) {
				delayLines[i] = delayBuffer.readBuffer(delayInSamples[i]);
				yn = yn + delayLines[i];

				weightedFeedback_Pct[i] = float(i) / 10.0;
				weightedFeedbackOutput = weightedFeedbackOutput + (delayLines[i] * weightedFeedback_Pct[i]);
			}

			yn = yn / 4.0;
			double dn = xn + ((parameters.feedback_Pct / 100.0) * weightedFeedbackOutput);
			delayBuffer.writeBuffer(dn);

			// --- done
			return (yn * parameters.blend) + (xn * (1.0 - parameters.blend));
		}
	}

	virtual void enableAuxInput(bool enableAuxInput) { parameters.enableSidechain = enableAuxInput; }

	virtual double processAuxInputAudioSample(double xn)
	{
		sidechainInputSample = xn;
		return sidechainInputSample;
	}

	/** query to see if this object can process frames */
	virtual bool canProcessAudioFrame() { return false; } // <-- change this!

	/** get parameters: note use of custom structure for passing param data */
	/**
	\return FourTapDelayParameters custom data structure
	*/
	FourTapDelayParameters getParameters()
	{
		return parameters;
	}

	/** set parameters: note use of custom structure for passing param data */
	/**
	\param FourTapDelayParameters custom data structure
	*/
	void setParameters(const FourTapDelayParameters& params)
	{
		// --- copy them; note you may choose to ignore certain items
		//     and copy the variables one at a time, or you may test
		//     to see if cook-able variables have changed; if not, then
		//     do not re-cook them as it just wastes CPU
		parameters = params;
		loadDelayTimes();
		delayInSamples[0] = delayTime_mSec[0] * (samplesPerMSec);
		delayInSamples[1] = delayTime_mSec[1] * (samplesPerMSec);
		delayInSamples[2] = delayTime_mSec[2] * (samplesPerMSec);
		delayInSamples[3] = delayTime_mSec[3] * (samplesPerMSec);

		SuperLFOParameters LFOparams = lfo.getParameters();
		LFOparams.frequency_Hz = parameters.modRate_Hz;
		lfo.setParameters(LFOparams);

		// --- cook parameters here
	}

	/** creation function */
	void createDelayBuffers(double _sampleRate, double _bufferLength_mSec)
	{
		sampleRate = _sampleRate;
		samplesPerMSec = sampleRate / 1000.0;
		// --- store for math
		bufferLength_mSec = _bufferLength_mSec;
		// --- total buffer length including fractional part
		bufferLength = (unsigned int)(bufferLength_mSec * (samplesPerMSec)) + 1; // +1 for fractional part

		// --- create new buffer
		delayBuffer.createCircularBuffer(bufferLength);
		
	}

	void loadDelayTimes() {
		switch (parameters.modeSelectorValue) {
		case 1:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = 0.0;
			delayTime_mSec[2] = 0.0;
			delayTime_mSec[3] = 0.0;
			break;
		case 2:
			delayTime_mSec[0] = parameters.delayTime_long;
			delayTime_mSec[1] = 0.0;
			delayTime_mSec[2] = 0.0;
			delayTime_mSec[3] = 0.0;
			break;
		case 3:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = 0.0;
			delayTime_mSec[3] = 0.0;
			break;
		case 4:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = 0.0;
			delayTime_mSec[3] = 0.0;
			break;
		case 5:
			delayTime_mSec[0] = parameters.delayTime_long;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = 0.0;
			delayTime_mSec[3] = 0.0;
			break;
		case 6:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_short + delayTime_mSec[1];
			delayTime_mSec[3] = 0.0;
			break;
		case 7:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = 0.0;
			break;
		case 8:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = 0.0;
			break;
		case 9:
			delayTime_mSec[0] = parameters.delayTime_long;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = 0.0;
			break;
		case 10:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_short + delayTime_mSec[1];
			delayTime_mSec[3] = parameters.delayTime_short + delayTime_mSec[2];
			break;
		case 11:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_short + delayTime_mSec[1];
			delayTime_mSec[3] = parameters.delayTime_long + delayTime_mSec[2];
			break;
		case 12:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_short + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = parameters.delayTime_long + delayTime_mSec[2];
			break;
		case 13:
			delayTime_mSec[0] = parameters.delayTime_short;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = parameters.delayTime_long + delayTime_mSec[2];
			break;
		case 14:
			delayTime_mSec[0] = parameters.delayTime_long;
			delayTime_mSec[1] = parameters.delayTime_long + delayTime_mSec[0];
			delayTime_mSec[2] = parameters.delayTime_long + delayTime_mSec[1];
			delayTime_mSec[3] = parameters.delayTime_long + delayTime_mSec[2];
			break;
		}
	}

private:
	FourTapDelayParameters parameters; ///< object parameters
	SuperLFO lfo;
	AudioDelay modDelay;
	AudioDetector detector;

	// --- local variables used by this object
	double sampleRate = 0.0;	///< sample rate

	double sidechainInputSample = 0.0;

	double samplesPerMSec = 0.0;	///< samples per millisecond, for easy access calculation
	double delayInSamples[4] = { 0.0, 0.0, 0.0, 0.0 };	///< double includes fractional part
	double bufferLength_mSec = 0.0;	///< buffer length in mSec
	unsigned int bufferLength = 0;	///< buffer length in samples
	double delayTime_mSec[4] = { 0.0, 0.0, 0.0, 0.0 };

	// Modulation Variables
	double minDelay_mSec[2][3] = { { 1.0, 0.0, 4.0 }, { 1.0, 0.0, 16.0 } };
	double modDepth_mSec[2][3] = { { 4.0, 5.0, 8.0 }, { 6.0, 7.0, 24.0 } };

	double modWet_dB[3] = { -3.0, 0.0, -3.0 };
	double modDry_dB[3] = { -3.0, -96.0, 0.0 };

	// --- delay buffer of doubles
	CircularBuffer<double> delayBuffer;
};

#endif