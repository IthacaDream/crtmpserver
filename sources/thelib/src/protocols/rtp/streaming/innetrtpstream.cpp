/* 
 *  Copyright (c) 2010,
 *  Gavriloaie Eugen-Andrei (shiretu@gmail.com)
 *
 *  This file is part of crtmpserver.
 *  crtmpserver is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  crtmpserver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with crtmpserver.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAS_PROTOCOL_RTP
#include "protocols/rtp/streaming/innetrtpstream.h"
#include "streaming/streamstypes.h"
#include "streaming/nalutypes.h"
#include "streaming/baseoutstream.h"
#include "protocols/baseprotocol.h"
#include "protocols/rtmp/basertmpprotocol.h"
#include "protocols/rtmp/streaming/baseoutnetrtmpstream.h"

InNetRTPStream::InNetRTPStream(BaseProtocol *pProtocol,
		StreamsManager *pStreamsManager, string name, string SPS, string PPS, string AAC)
: BaseInNetStream(pProtocol, pStreamsManager, ST_IN_NET_RTP, name) {
	_counter = 0;
	_lastVideoTs = 0;
	_lastAudioTs = 0;

	_capabilities.InitAudioAAC(
			(uint8_t *) STR(AAC),
			AAC.length());
	_capabilities.InitVideoH264(
			(uint8_t *) STR(SPS),
			SPS.length(),
			(uint8_t *) STR(PPS),
			PPS.length());
}

InNetRTPStream::~InNetRTPStream() {
}

StreamCapabilities * InNetRTPStream::GetCapabilities() {
	return &_capabilities;
}

bool InNetRTPStream::IsCompatibleWithType(uint64_t type) {
	return type == ST_OUT_NET_RTMP_4_TS;
}

void InNetRTPStream::ReadyForSend() {

}

void InNetRTPStream::SignalOutStreamAttached(BaseOutStream *pOutStream) {
	if (TAG_KIND_OF(pOutStream->GetType(), ST_OUT_NET_RTMP)) {
		((BaseOutNetRTMPStream *) pOutStream)->CanDropFrames(true);
	}
	if (_lastVideoTs != 0) {
		if (!pOutStream->FeedData(
				_capabilities.avc._pSPS,
				_capabilities.avc._spsLength,
				0,
				_capabilities.avc._spsLength,
				_lastVideoTs,
				false)) {
			FATAL("Unable to feed stream");
			if (pOutStream->GetProtocol() != NULL) {
				pOutStream->GetProtocol()->EnqueueForDelete();
			}
		}
		if (!pOutStream->FeedData(
				_capabilities.avc._pPPS,
				_capabilities.avc._ppsLength,
				0,
				_capabilities.avc._ppsLength,
				_lastVideoTs,
				false)) {
			FATAL("Unable to feed stream");
			if (pOutStream->GetProtocol() != NULL) {
				pOutStream->GetProtocol()->EnqueueForDelete();
			}
		}
	}
	if (_lastAudioTs != 0) {
		if (!pOutStream->FeedData(
				_capabilities.aac._pAAC,
				_capabilities.aac._aacLength,
				0,
				_capabilities.aac._aacLength,
				_lastAudioTs,
				true)) {
			FATAL("Unable to feed stream");
			if (pOutStream->GetProtocol() != NULL) {
				pOutStream->GetProtocol()->EnqueueForDelete();
			}
		}
	}
#ifdef HAS_PROTOCOL_RTMP
	if (TAG_KIND_OF(pOutStream->GetType(), ST_OUT_NET_RTMP)) {
		((BaseRTMPProtocol *) pOutStream->GetProtocol())->TrySetOutboundChunkSize(4 * 1024 * 1024);
		((BaseOutNetRTMPStream *) pOutStream)->CanDropFrames(true);
	}
#endif /* HAS_PROTOCOL_RTMP */
}

void InNetRTPStream::SignalOutStreamDetached(BaseOutStream *pOutStream) {
	//NYIA;
}

bool InNetRTPStream::SignalPlay(double &absoluteTimestamp, double &length) {
	return true;
}

bool InNetRTPStream::SignalPause() {
	FATAL("Pause is not supported on inbound RTSP streams");
	return false;
	//return true;
}

bool InNetRTPStream::SignalResume() {
	FATAL("Resume is not supported on inbound RTSP streams");
	return false;
}

bool InNetRTPStream::SignalSeek(double &absoluteTimestamp) {
	FATAL("Seek is not supported on inbound RTSP streams");
	return false;
}

bool InNetRTPStream::SignalStop() {
	return true;
}

bool InNetRTPStream::FeedData(uint8_t *pData, uint32_t dataLength,
		uint32_t processedLength, uint32_t totalLength,
		double absoluteTimestamp, bool isAudio) {
	double &lastTs = isAudio ? _lastAudioTs : _lastVideoTs;

	if ((uint64_t) (lastTs * 100.00) == (uint64_t) (absoluteTimestamp * 100.00)) {
		absoluteTimestamp = lastTs;
	}

	if ((uint64_t) (lastTs * 100.00) > (uint64_t) (absoluteTimestamp * 100.00)) {
		WARN("Back time on %s. ATS: %.08f LTS: %.08f; D: %.8f",
				STR(GetName()),
				absoluteTimestamp,
				lastTs,
				absoluteTimestamp - lastTs);
		return true;
	}
	LinkedListNode<BaseOutStream *> *pTemp = _pOutStreams;
	if (lastTs == 0) {
		lastTs = absoluteTimestamp;
		while (pTemp != NULL) {
			if (!pTemp->info->IsEnqueueForDelete()) {
				SignalOutStreamAttached(pTemp->info);
			}
			pTemp = pTemp->pPrev;
		}
	}
	lastTs = absoluteTimestamp;
	pTemp = _pOutStreams;
	while (pTemp != NULL) {
		if (!pTemp->info->IsEnqueueForDelete()) {
			if (!pTemp->info->FeedData(pData, dataLength, processedLength, totalLength,
					absoluteTimestamp, isAudio)) {
				WARN("Unable to feed OS: %p", pTemp->info);
				pTemp->info->EnqueueForDelete();
				if (GetProtocol() == pTemp->info->GetProtocol()) {
					return false;
				}
			}
		}
		pTemp = pTemp->pPrev;
	}
	return true;
}

bool InNetRTPStream::FeedVideoData(uint8_t *pData, uint32_t dataLength,
		RTPHeader &rtpHeader) {
	//1. Check the counter first
	if (_counter == 0) {
		//this is the first packet. Make sure we start with a M packet
		if (!GET_RTP_M(rtpHeader)) {
			return true;
		}
		_counter = GET_RTP_SEQ(rtpHeader);
		return true;
	} else {
		if (_counter + 1 != GET_RTP_SEQ(rtpHeader)) {
			//WARN("Missing packet");
			_currentNalu.IgnoreAll();
			_counter = 0;
			return true;
		} else {
			_counter++;
		}
	}

	//2. get the nalu
	double ts = (double) rtpHeader._timestamp / _capabilities.avc._rate * 1000.0;
	uint8_t naluType = NALU_TYPE(pData[0]);
	if (naluType <= 23) {
		//3. Standard NALU
		return FeedData(pData, dataLength, 0, dataLength, ts, false);
	} else if (naluType == NALU_TYPE_FUA) {
		if (GETAVAILABLEBYTESCOUNT(_currentNalu) == 0) {
			_currentNalu.IgnoreAll();
			//start NAL
			if ((pData[1] >> 7) == 0) {
				WARN("Bogus nalu");
				_currentNalu.IgnoreAll();
				_counter = 0;
				return true;
			}
			pData[1] = (pData[0]&0xe0) | (pData[1]&0x1f);
			_currentNalu.ReadFromBuffer(pData + 1, dataLength - 1);
			return true;
		} else {
			//middle NAL
			_currentNalu.ReadFromBuffer(pData + 2, dataLength - 2);
			if (((pData[1] >> 6)&0x01) == 1) {
				if (!FeedData(GETIBPOINTER(_currentNalu),
						GETAVAILABLEBYTESCOUNT(_currentNalu), 0,
						GETAVAILABLEBYTESCOUNT(_currentNalu),
						ts,
						false)) {
					FATAL("Unable to feed NALU");
					return false;
				}
				_currentNalu.IgnoreAll();
			}
			return true;
		}
	} else if (naluType == NALU_TYPE_STAPA) {
		uint32_t index = 1;
		//FINEST("ts: %.2f; delta: %.2f", ts, ts - _lastTs);
		while (index + 3 < dataLength) {
			uint16_t length = ENTOHSP(pData + index);
			index += 2;
			if (index + length > dataLength) {
				WARN("Bogus STAP-A");
				_currentNalu.IgnoreAll();
				_counter = 0;
				return true;
			}
			if (!FeedData(pData + index,
					length, 0,
					length,
					ts, false)) {
				FATAL("Unable to feed NALU");
				return false;
			}
			index += length;
		}
		return true;
	} else {
		WARN("invalid NAL: %s", STR(NALUToString(naluType)));
		_currentNalu.IgnoreAll();
		_counter = 0;
		return true;
	}
}

//double ____last = 0;

bool InNetRTPStream::FeedAudioData(uint8_t *pData, uint32_t dataLength,
		RTPHeader &rtpHeader) {
	//1. Compute AUHeadersLength in bytes
	uint16_t auHeadersLength = ENTOHSP(pData);
	if ((auHeadersLength % 8) != 0) {
		FATAL("Invalid AU headers length: %04x", auHeadersLength);
		return false;
	}

	//2. apply it to the buffer
	auHeadersLength = auHeadersLength / 8 + 2;
	if (auHeadersLength >= dataLength) {
		FATAL("Invalid AU headers length: %04x", auHeadersLength);
		return false;
	}
	pData += auHeadersLength;
	dataLength -= auHeadersLength;

	//3. Compute the ts
	double ts = (double) rtpHeader._timestamp / _capabilities.aac._sampleRate * 1000.00;
	//	FINEST("ts: %.8f; diff: %.08f; dataLength: %d; auHeadersLength: %d",
	//			ts / 1000.00, (ts - ____last) / 1000.00, dataLength, auHeadersLength - 2);
	//	____last = ts;


	//	FINEST("auHeadersLength: %d; dataLength: %d; %.02f; %02x %02x %02x %02x %02x %02x %02x %02x",
	//			auHeadersLength, dataLength,
	//			ts,
	//			pData[0], pData[1], pData[2], pData[3],
	//			pData[4], pData[5], pData[6], pData[7]);
	//	return true;

	return FeedData(pData,
			dataLength,
			0,
			dataLength,
			ts, true);
}
#endif /* HAS_PROTOCOL_RTP */
