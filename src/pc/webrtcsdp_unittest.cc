/*
 *  Copyright 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "api/jsepsessiondescription.h"
#include "media/base/mediaconstants.h"
#include "media/engine/webrtcvideoengine.h"
#include "p2p/base/port.h"
#include "p2p/base/p2pconstants.h"
#include "pc/mediasession.h"
#include "rtc_base/checks.h"
#include "rtc_base/gunit.h"
#include "rtc_base/logging.h"
#include "rtc_base/stringencode.h"
#include "rtc_base/stringutils.h"

#ifdef WEBRTC_ANDROID
#include "pc/test/androidtestinitializer.h"
#endif
#include "pc/webrtcsdp.h"

using cricket::AudioCodec;
using cricket::AudioContentDescription;
using cricket::Candidate;
using cricket::ContentInfo;
using cricket::CryptoParams;
using cricket::ContentGroup;
using cricket::DataCodec;
using cricket::DataContentDescription;
using cricket::ICE_CANDIDATE_COMPONENT_RTCP;
using cricket::ICE_CANDIDATE_COMPONENT_RTP;
using cricket::kFecSsrcGroupSemantics;
using cricket::LOCAL_PORT_TYPE;
using cricket::RELAY_PORT_TYPE;
using cricket::SessionDescription;
using cricket::MediaProtocolType;
using cricket::StreamParams;
using cricket::STUN_PORT_TYPE;
using cricket::TransportDescription;
using cricket::TransportInfo;
using cricket::VideoCodec;
using cricket::VideoContentDescription;
using webrtc::IceCandidateCollection;
using webrtc::IceCandidateInterface;
using webrtc::JsepIceCandidate;
using webrtc::JsepSessionDescription;
using webrtc::RtpExtension;
using webrtc::RtpTransceiverDirection;
using webrtc::SdpParseError;
using webrtc::SdpType;
using webrtc::SessionDescriptionInterface;

typedef std::vector<AudioCodec> AudioCodecs;
typedef std::vector<Candidate> Candidates;

static const uint32_t kDefaultSctpPort = 5000;
static const char kDefaultSctpPortStr[] = "5000";
static const uint16_t kUnusualSctpPort = 9556;
static const char kUnusualSctpPortStr[] = "9556";
static const char kSessionTime[] = "t=0 0\r\n";
static const uint32_t kCandidatePriority = 2130706432U;  // pref = 1.0
static const char kAttributeIceUfragVoice[] = "a=ice-ufrag:ufrag_voice\r\n";
static const char kAttributeIcePwdVoice[] = "a=ice-pwd:pwd_voice\r\n";
static const char kAttributeIceUfragVideo[] = "a=ice-ufrag:ufrag_video\r\n";
static const char kAttributeIcePwdVideo[] = "a=ice-pwd:pwd_video\r\n";
static const uint32_t kCandidateGeneration = 2;
static const char kCandidateFoundation1[] = "a0+B/1";
static const char kCandidateFoundation2[] = "a0+B/2";
static const char kCandidateFoundation3[] = "a0+B/3";
static const char kCandidateFoundation4[] = "a0+B/4";
static const char kAttributeCryptoVoice[] =
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n";
static const char kAttributeCryptoVideo[] =
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n";
static const char kFingerprint[] = "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";
static const int kExtmapId = 1;
static const char kExtmapUri[] = "http://example.com/082005/ext.htm#ttime";
static const char kExtmap[] =
    "a=extmap:1 http://example.com/082005/ext.htm#ttime\r\n";
static const char kExtmapWithDirectionAndAttribute[] =
    "a=extmap:1/sendrecv http://example.com/082005/ext.htm#ttime a1 a2\r\n";
static const char kExtmapWithDirectionAndAttributeEncrypted[] =
    "a=extmap:1/sendrecv urn:ietf:params:rtp-hdrext:encrypt "
    "http://example.com/082005/ext.htm#ttime a1 a2\r\n";

static const uint8_t kIdentityDigest[] = {
    0x4A, 0xAD, 0xB9, 0xB1, 0x3F, 0x82, 0x18, 0x3B, 0x54, 0x02,
    0x12, 0xDF, 0x3E, 0x5D, 0x49, 0x6B, 0x19, 0xE5, 0x7C, 0xAB};

static const char kDtlsSctp[] = "DTLS/SCTP";
static const char kUdpDtlsSctp[] = "UDP/DTLS/SCTP";
static const char kTcpDtlsSctp[] = "TCP/DTLS/SCTP";

struct CodecParams {
  int max_ptime;
  int ptime;
  int min_ptime;
  int sprop_stereo;
  int stereo;
  int useinband;
  int maxaveragebitrate;
};

// TODO(deadbeef): In these reference strings, use "a=fingerprint" by default
// instead of "a=crypto", and have an explicit test for adding "a=crypto".
// Currently it's the other way around.

// Reference sdp string
static const char kSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n"
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:video_track_id_1\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:video_track_id_1\r\n";

// SDP reference string without the candidates.
static const char kSdpString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n"
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:video_track_id_1\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:video_track_id_1\r\n";

static const char kSdpRtpDataChannelString[] =
    "m=application 9 RTP/SAVPF 101\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=mid:data_content_name\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:FvLcvU2P3ZWmQxgPAgcDu7Zl9vftYElFOjEzhWs5\r\n"
    "a=rtpmap:101 google-data/90000\r\n"
    "a=ssrc:10 cname:data_channel_cname\r\n"
    "a=ssrc:10 msid:data_channel data_channeld0\r\n"
    "a=ssrc:10 mslabel:data_channel\r\n"
    "a=ssrc:10 label:data_channeld0\r\n";

static const char kSdpSctpDataChannelString[] =
    "m=application 9 DTLS/SCTP 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=mid:data_content_name\r\n"
    "a=sctpmap:5000 webrtc-datachannel 1024\r\n";

// draft-ietf-mmusic-sctp-sdp-12
static const char kSdpSctpDataChannelStringWithSctpPort[] =
    "m=application 9 DTLS/SCTP webrtc-datachannel\r\n"
    "a=max-message-size=100000\r\n"
    "a=sctp-port 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=mid:data_content_name\r\n";

static const char kSdpSctpDataChannelStringWithSctpColonPort[] =
    "m=application 9 DTLS/SCTP webrtc-datachannel\r\n"
    "a=max-message-size=100000\r\n"
    "a=sctp-port:5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=mid:data_content_name\r\n";

static const char kSdpSctpDataChannelWithCandidatesString[] =
    "m=application 2345 DTLS/SCTP 5000\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=mid:data_content_name\r\n"
    "a=sctpmap:5000 webrtc-datachannel 1024\r\n";

static const char kSdpConferenceString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=x-google-flag:conference\r\n"
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=x-google-flag:conference\r\n";

static const char kSdpSessionString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream\r\n";

static const char kSdpAudioString[] =
    "m=audio 9 RTP/SAVPF 111\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n";

static const char kSdpVideoString[] =
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream video_track_id_1\r\n"
    "a=ssrc:2 mslabel:local_stream\r\n"
    "a=ssrc:2 label:video_track_id_1\r\n";

// Reference sdp string using bundle-only.
static const char kBundleOnlySdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE audio_content_name video_content_name\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n"
    "m=video 0 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=bundle-only\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:video_track_id_1\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:video_track_id_1\r\n";

// Plan B SDP reference string, with 2 streams, 2 audio tracks and 3 video
// tracks.
static const char kPlanBSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1 local_stream_2\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    "a=ssrc:4 msid:local_stream_2 audio_track_id_2\r\n"
    "a=ssrc:4 mslabel:local_stream_2\r\n"
    "a=ssrc:4 label:audio_track_id_2\r\n"
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:video_track_id_1\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:video_track_id_1\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    "a=ssrc:5 msid:local_stream_2 video_track_id_2\r\n"
    "a=ssrc:5 mslabel:local_stream_2\r\n"
    "a=ssrc:5 label:video_track_id_2\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n"
    "a=ssrc:6 msid:local_stream_2 video_track_id_3\r\n"
    "a=ssrc:6 mslabel:local_stream_2\r\n"
    "a=ssrc:6 label:video_track_id_3\r\n";

// Unified Plan SDP reference string, with 2 streams, 2 audio tracks and 3 video
// tracks.
static const char kUnifiedPlanSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    // Audio track 1, stream 1 (with candidates).
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    // Video track 1, stream 1 (with candidates).
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=mid:video_content_name\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    // Audio track 2, stream 2.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_2\r\na=ice-pwd:pwd_voice_2\r\n"
    "a=mid:audio_content_name_2\r\n"
    "a=msid:local_stream_2 audio_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    // Video track 2, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_2\r\na=ice-pwd:pwd_video_2\r\n"
    "a=mid:video_content_name_2\r\n"
    "a=msid:local_stream_2 video_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    // Video track 3, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_3\r\na=ice-pwd:pwd_video_3\r\n"
    "a=mid:video_content_name_3\r\n"
    "a=msid:local_stream_2 video_track_id_3\r\n"
    "a=sendrecv\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n";

// Unified Plan SDP reference string:
// - audio track 1 has 1 a=msid lines
// - audio track 2 has 2 a=msid lines
// - audio track 3 has 1 a=msid line with the special "-" marker signifying that
//   there are 0 media stream ids.
// This Unified Plan SDP represents a SDP that signals the msid using both
// a=msid and a=ssrc msid semantics.
static const char kUnifiedPlanSdpFullStringWithSpecialMsid[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    // Audio track 1, with 1 stream id.
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=mid:audio_content_name\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:audio_track_id_1\r\n"
    // Audio track 2, with two stream ids.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_2\r\na=ice-pwd:pwd_voice_2\r\n"
    "a=mid:audio_content_name_2\r\n"
    "a=msid:local_stream_1 audio_track_id_2\r\n"
    "a=msid:local_stream_2 audio_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:4 cname:stream_1_cname\r\n"
    // The support for Plan B msid signaling only includes the
    // first media stream id "local_stream_1."
    "a=ssrc:4 msid:local_stream_1 audio_track_id_2\r\n"
    "a=ssrc:4 mslabel:local_stream_1\r\n"
    "a=ssrc:4 label:audio_track_id_2\r\n"
    // Audio track 3, with no stream ids.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_3\r\na=ice-pwd:pwd_voice_3\r\n"
    "a=mid:audio_content_name_3\r\n"
    "a=msid:- audio_track_id_3\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:7 cname:stream_2_cname\r\n";

// One candidate reference string as per W3c spec.
// candidate:<blah> not a=candidate:<blah>CRLF
static const char kRawCandidate[] =
    "candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2";
// One candidate reference string.
static const char kSdpOneCandidate[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n";

static const char kSdpTcpActiveCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype active generation 2";
static const char kSdpTcpPassiveCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype passive generation 2";
static const char kSdpTcpSOCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype so generation 2";
static const char kSdpTcpInvalidCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype invalid generation 2";

// One candidate reference string with IPV6 address.
static const char kRawIPV6Candidate[] =
    "candidate:a0+B/1 1 udp 2130706432 "
    "abcd:abcd:abcd:abcd:abcd:abcd:abcd:abcd 1234 typ host generation 2";

// One candidate reference string.
static const char kSdpOneCandidateWithUfragPwd[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host network_name"
    " eth0 ufrag user_rtp pwd password_rtp generation 2\r\n";

// Session id and version
static const char kSessionId[] = "18446744069414584320";
static const char kSessionVersion[] = "18446462598732840960";

// ICE options.
static const char kIceOption1[] = "iceoption1";
static const char kIceOption2[] = "iceoption2";
static const char kIceOption3[] = "iceoption3";

// ICE ufrags/passwords.
static const char kUfragVoice[] = "ufrag_voice";
static const char kPwdVoice[] = "pwd_voice";
static const char kUfragVideo[] = "ufrag_video";
static const char kPwdVideo[] = "pwd_video";
static const char kUfragData[] = "ufrag_data";
static const char kPwdData[] = "pwd_data";

// Extra ufrags/passwords for extra unified plan m= sections.
static const char kUfragVoice2[] = "ufrag_voice_2";
static const char kPwdVoice2[] = "pwd_voice_2";
static const char kUfragVoice3[] = "ufrag_voice_3";
static const char kPwdVoice3[] = "pwd_voice_3";
static const char kUfragVideo2[] = "ufrag_video_2";
static const char kPwdVideo2[] = "pwd_video_2";
static const char kUfragVideo3[] = "ufrag_video_3";
static const char kPwdVideo3[] = "pwd_video_3";

// Content name
static const char kAudioContentName[] = "audio_content_name";
static const char kVideoContentName[] = "video_content_name";
static const char kDataContentName[] = "data_content_name";

// Extra content names for extra unified plan m= sections.
static const char kAudioContentName2[] = "audio_content_name_2";
static const char kAudioContentName3[] = "audio_content_name_3";
static const char kVideoContentName2[] = "video_content_name_2";
static const char kVideoContentName3[] = "video_content_name_3";

// MediaStream 1
static const char kStreamId1[] = "local_stream_1";
static const char kStream1Cname[] = "stream_1_cname";
static const char kAudioTrackId1[] = "audio_track_id_1";
static const uint32_t kAudioTrack1Ssrc = 1;
static const char kVideoTrackId1[] = "video_track_id_1";
static const uint32_t kVideoTrack1Ssrc1 = 2;
static const uint32_t kVideoTrack1Ssrc2 = 3;

// MediaStream 2
static const char kStreamId2[] = "local_stream_2";
static const char kStream2Cname[] = "stream_2_cname";
static const char kAudioTrackId2[] = "audio_track_id_2";
static const uint32_t kAudioTrack2Ssrc = 4;
static const char kVideoTrackId2[] = "video_track_id_2";
static const uint32_t kVideoTrack2Ssrc = 5;
static const char kVideoTrackId3[] = "video_track_id_3";
static const uint32_t kVideoTrack3Ssrc = 6;
static const char kAudioTrackId3[] = "audio_track_id_3";
static const uint32_t kAudioTrack3Ssrc = 7;

// DataChannel
static const char kDataChannelLabel[] = "data_channel";
static const char kDataChannelMsid[] = "data_channeld0";
static const char kDataChannelCname[] = "data_channel_cname";
static const uint32_t kDataChannelSsrc = 10;

// Candidate
static const char kDummyMid[] = "dummy_mid";
static const int kDummyIndex = 123;

// Misc
static SdpType kDummyType = SdpType::kOffer;

// Helper functions

static bool SdpDeserialize(const std::string& message,
                           JsepSessionDescription* jdesc) {
  return webrtc::SdpDeserialize(message, jdesc, NULL);
}

static bool SdpDeserializeCandidate(const std::string& message,
                                    JsepIceCandidate* candidate) {
  return webrtc::SdpDeserializeCandidate(message, candidate, NULL);
}

// Add some extra |newlines| to the |message| after |line|.
static void InjectAfter(const std::string& line,
                        const std::string& newlines,
                        std::string* message) {
  const std::string tmp = line + newlines;
  rtc::replace_substrs(line.c_str(), line.length(),
                             tmp.c_str(), tmp.length(), message);
}

static void Replace(const std::string& line,
                    const std::string& newlines,
                    std::string* message) {
  rtc::replace_substrs(line.c_str(), line.length(),
                             newlines.c_str(), newlines.length(), message);
}

// Expect fail to parase |bad_sdp| and expect |bad_part| be part of the error
// message.
static void ExpectParseFailure(const std::string& bad_sdp,
                               const std::string& bad_part) {
  JsepSessionDescription desc(kDummyType);
  SdpParseError error;
  bool ret = webrtc::SdpDeserialize(bad_sdp, &desc, &error);
  EXPECT_FALSE(ret);
  EXPECT_NE(std::string::npos, error.line.find(bad_part.c_str()));
}

// Expect fail to parse kSdpFullString if replace |good_part| with |bad_part|.
static void ExpectParseFailure(const char* good_part, const char* bad_part) {
  std::string bad_sdp = kSdpFullString;
  Replace(good_part, bad_part, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

// Expect fail to parse kSdpFullString if add |newlines| after |injectpoint|.
static void ExpectParseFailureWithNewLines(const std::string& injectpoint,
                                           const std::string& newlines,
                                           const std::string& bad_part) {
  std::string bad_sdp = kSdpFullString;
  InjectAfter(injectpoint, newlines, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

static void ReplaceDirection(RtpTransceiverDirection direction,
                             std::string* message) {
  std::string new_direction;
  switch (direction) {
    case RtpTransceiverDirection::kInactive:
      new_direction = "a=inactive";
      break;
    case RtpTransceiverDirection::kSendOnly:
      new_direction = "a=sendonly";
      break;
    case RtpTransceiverDirection::kRecvOnly:
      new_direction = "a=recvonly";
      break;
    case RtpTransceiverDirection::kSendRecv:
    default:
      new_direction = "a=sendrecv";
      break;
  }
  Replace("a=sendrecv", new_direction, message);
}

static void ReplaceRejected(bool audio_rejected, bool video_rejected,
                            std::string* message) {
  if (audio_rejected) {
    Replace("m=audio 9", "m=audio 0", message);
    Replace(kAttributeIceUfragVoice, "", message);
    Replace(kAttributeIcePwdVoice, "", message);
  }
  if (video_rejected) {
    Replace("m=video 9", "m=video 0", message);
    Replace(kAttributeIceUfragVideo, "", message);
    Replace(kAttributeIcePwdVideo, "", message);
  }
}

// WebRtcSdpTest

class WebRtcSdpTest : public testing::Test {
 public:
  WebRtcSdpTest() : jdesc_(kDummyType) {
#ifdef WEBRTC_ANDROID
    webrtc::InitializeAndroidObjects();
#endif
    // AudioContentDescription
    audio_desc_ = CreateAudioContentDescription();
    StreamParams audio_stream;
    audio_stream.id = kAudioTrackId1;
    audio_stream.cname = kStream1Cname;
    audio_stream.set_stream_ids({kStreamId1});
    audio_stream.ssrcs.push_back(kAudioTrack1Ssrc);
    audio_desc_->AddStream(audio_stream);
    rtc::SocketAddress audio_addr("74.125.127.126", 2345);
    audio_desc_->set_connection_address(audio_addr);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_desc_);

    // VideoContentDescription
    video_desc_ = CreateVideoContentDescription();
    StreamParams video_stream;
    video_stream.id = kVideoTrackId1;
    video_stream.cname = kStream1Cname;
    video_stream.set_stream_ids({kStreamId1});
    video_stream.ssrcs.push_back(kVideoTrack1Ssrc1);
    video_stream.ssrcs.push_back(kVideoTrack1Ssrc2);
    cricket::SsrcGroup ssrc_group(kFecSsrcGroupSemantics, video_stream.ssrcs);
    video_stream.ssrc_groups.push_back(ssrc_group);
    video_desc_->AddStream(video_stream);
    rtc::SocketAddress video_addr("74.125.224.39", 3457);
    video_desc_->set_connection_address(video_addr);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_desc_);

    // TransportInfo
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kAudioContentName, TransportDescription(kUfragVoice, kPwdVoice))));
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kVideoContentName, TransportDescription(kUfragVideo, kPwdVideo))));

    // v4 host
    int port = 1234;
    rtc::SocketAddress address("192.168.1.5", port++);
    Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "udp", address,
                         kCandidatePriority, "", "", LOCAL_PORT_TYPE,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address,
                         kCandidatePriority, "", "", LOCAL_PORT_TYPE,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate3(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address,
                         kCandidatePriority, "", "", LOCAL_PORT_TYPE,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate4(ICE_CANDIDATE_COMPONENT_RTP, "udp", address,
                         kCandidatePriority, "", "", LOCAL_PORT_TYPE,
                         kCandidateGeneration, kCandidateFoundation1);

    // v6 host
    rtc::SocketAddress v6_address("::1", port++);
    cricket::Candidate candidate5(cricket::ICE_CANDIDATE_COMPONENT_RTP, "udp",
                                  v6_address, kCandidatePriority, "", "",
                                  cricket::LOCAL_PORT_TYPE,
                                  kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate6(cricket::ICE_CANDIDATE_COMPONENT_RTCP, "udp",
                                  v6_address, kCandidatePriority, "", "",
                                  cricket::LOCAL_PORT_TYPE,
                                  kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate7(cricket::ICE_CANDIDATE_COMPONENT_RTCP, "udp",
                                  v6_address, kCandidatePriority, "", "",
                                  cricket::LOCAL_PORT_TYPE,
                                  kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate8(cricket::ICE_CANDIDATE_COMPONENT_RTP, "udp",
                                  v6_address, kCandidatePriority, "", "",
                                  cricket::LOCAL_PORT_TYPE,
                                  kCandidateGeneration, kCandidateFoundation2);

    // stun
    int port_stun = 2345;
    rtc::SocketAddress address_stun("74.125.127.126", port_stun++);
    rtc::SocketAddress rel_address_stun("192.168.1.5", port_stun++);
    cricket::Candidate candidate9(cricket::ICE_CANDIDATE_COMPONENT_RTP, "udp",
                                  address_stun, kCandidatePriority, "", "",
                                  STUN_PORT_TYPE, kCandidateGeneration,
                                  kCandidateFoundation3);
    candidate9.set_related_address(rel_address_stun);

    address_stun.SetPort(port_stun++);
    rel_address_stun.SetPort(port_stun++);
    cricket::Candidate candidate10(cricket::ICE_CANDIDATE_COMPONENT_RTCP, "udp",
                                   address_stun, kCandidatePriority, "", "",
                                   STUN_PORT_TYPE, kCandidateGeneration,
                                   kCandidateFoundation3);
    candidate10.set_related_address(rel_address_stun);

    // relay
    int port_relay = 3456;
    rtc::SocketAddress address_relay("74.125.224.39", port_relay++);
    cricket::Candidate candidate11(cricket::ICE_CANDIDATE_COMPONENT_RTCP, "udp",
                                   address_relay, kCandidatePriority, "", "",
                                   cricket::RELAY_PORT_TYPE,
                                   kCandidateGeneration, kCandidateFoundation4);
    address_relay.SetPort(port_relay++);
    cricket::Candidate candidate12(cricket::ICE_CANDIDATE_COMPONENT_RTP, "udp",
                                   address_relay, kCandidatePriority, "", "",
                                   RELAY_PORT_TYPE, kCandidateGeneration,
                                   kCandidateFoundation4);

    // voice
    candidates_.push_back(candidate1);
    candidates_.push_back(candidate2);
    candidates_.push_back(candidate5);
    candidates_.push_back(candidate6);
    candidates_.push_back(candidate9);
    candidates_.push_back(candidate10);

    // video
    candidates_.push_back(candidate3);
    candidates_.push_back(candidate4);
    candidates_.push_back(candidate7);
    candidates_.push_back(candidate8);
    candidates_.push_back(candidate11);
    candidates_.push_back(candidate12);

    jcandidate_.reset(new JsepIceCandidate(std::string("audio_content_name"),
                                           0, candidate1));

    // Set up JsepSessionDescription.
    jdesc_.Initialize(desc_.Copy(), kSessionId, kSessionVersion);
    std::string mline_id;
    int mline_index = 0;
    for (size_t i = 0; i< candidates_.size(); ++i) {
      // In this test, the audio m line index will be 0, and the video m line
      // will be 1.
      bool is_video = (i > 5);
      mline_id = is_video ? "video_content_name" : "audio_content_name";
      mline_index = is_video ? 1 : 0;
      JsepIceCandidate jice(mline_id,
                            mline_index,
                            candidates_.at(i));
      jdesc_.AddCandidate(&jice);
    }
  }

  void RemoveVideoCandidates() {
    const IceCandidateCollection* video_candidates_collection =
        jdesc_.candidates(1);
    ASSERT_NE(nullptr, video_candidates_collection);
    std::vector<cricket::Candidate> video_candidates;
    for (size_t i = 0; i < video_candidates_collection->count(); ++i) {
      cricket::Candidate c = video_candidates_collection->at(i)->candidate();
      c.set_transport_name("video_content_name");
      video_candidates.push_back(c);
    }
    jdesc_.RemoveCandidates(video_candidates);
  }

  // Turns the existing reference description into a description using
  // a=bundle-only. This means no transport attributes and a 0 port value on
  // the m= sections not associated with the BUNDLE-tag.
  void MakeBundleOnlyDescription() {
    RemoveVideoCandidates();

    // And the rest of the transport attributes.
    desc_.transport_infos()[1].description.ice_ufrag.clear();
    desc_.transport_infos()[1].description.ice_pwd.clear();
    desc_.transport_infos()[1].description.connection_role =
        cricket::CONNECTIONROLE_NONE;

    // Set bundle-only flag.
    desc_.contents()[1].bundle_only = true;

    // Add BUNDLE group.
    ContentGroup group(cricket::GROUP_TYPE_BUNDLE);
    group.AddContentName(kAudioContentName);
    group.AddContentName(kVideoContentName);
    desc_.AddGroup(group);

    ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(), jdesc_.session_id(),
                                  jdesc_.session_version()));
  }

  // Turns the existing reference description into a plan B description,
  // with 2 audio tracks and 3 video tracks.
  void MakePlanBDescription() {
    audio_desc_ = audio_desc_->Copy();
    video_desc_ = video_desc_->Copy();

    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.cname = kStream2Cname;
    audio_track_2.set_stream_ids({kStreamId2});
    audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio_desc_->AddStream(audio_track_2);

    StreamParams video_track_2;
    video_track_2.id = kVideoTrackId2;
    video_track_2.cname = kStream2Cname;
    video_track_2.set_stream_ids({kStreamId2});
    video_track_2.ssrcs.push_back(kVideoTrack2Ssrc);
    video_desc_->AddStream(video_track_2);

    StreamParams video_track_3;
    video_track_3.id = kVideoTrackId3;
    video_track_3.cname = kStream2Cname;
    video_track_3.set_stream_ids({kStreamId2});
    video_track_3.ssrcs.push_back(kVideoTrack3Ssrc);
    video_desc_->AddStream(video_track_3);

    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_desc_);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_desc_);

    ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(), jdesc_.session_id(),
                                  jdesc_.session_version()));
  }

  // Turns the existing reference description into a unified plan description,
  // with 2 audio tracks and 3 video tracks.
  void MakeUnifiedPlanDescription() {
    // Audio track 2.
    AudioContentDescription* audio_desc_2 = CreateAudioContentDescription();
    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.cname = kStream2Cname;
    audio_track_2.set_stream_ids({kStreamId2});
    audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio_desc_2->AddStream(audio_track_2);
    desc_.AddContent(kAudioContentName2, MediaProtocolType::kRtp, audio_desc_2);
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kAudioContentName2, TransportDescription(kUfragVoice2, kPwdVoice2))));
    // Video track 2, in stream 2.
    VideoContentDescription* video_desc_2 = CreateVideoContentDescription();
    StreamParams video_track_2;
    video_track_2.id = kVideoTrackId2;
    video_track_2.cname = kStream2Cname;
    video_track_2.set_stream_ids({kStreamId2});
    video_track_2.ssrcs.push_back(kVideoTrack2Ssrc);
    video_desc_2->AddStream(video_track_2);
    desc_.AddContent(kVideoContentName2, MediaProtocolType::kRtp, video_desc_2);
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kVideoContentName2, TransportDescription(kUfragVideo2, kPwdVideo2))));

    // Video track 3, in stream 2.
    VideoContentDescription* video_desc_3 = CreateVideoContentDescription();
    StreamParams video_track_3;
    video_track_3.id = kVideoTrackId3;
    video_track_3.cname = kStream2Cname;
    video_track_3.set_stream_ids({kStreamId2});
    video_track_3.ssrcs.push_back(kVideoTrack3Ssrc);
    video_desc_3->AddStream(video_track_3);
    desc_.AddContent(kVideoContentName3, MediaProtocolType::kRtp, video_desc_3);
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kVideoContentName3, TransportDescription(kUfragVideo3, kPwdVideo3))));
    desc_.set_msid_signaling(cricket::kMsidSignalingMediaSection);

    ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(), jdesc_.session_id(),
                                  jdesc_.session_version()));
  }

  // Creates an audio content description with no streams, and some default
  // configuration.
  AudioContentDescription* CreateAudioContentDescription() {
    AudioContentDescription* audio = new AudioContentDescription();
    audio->set_rtcp_mux(true);
    audio->set_rtcp_reduced_size(true);
    audio->AddCrypto(CryptoParams(1, "AES_CM_128_HMAC_SHA1_32",
        "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32",
        "dummy_session_params"));
    audio->set_protocol(cricket::kMediaProtocolSavpf);
    AudioCodec opus(111, "opus", 48000, 0, 2);
    audio->AddCodec(opus);
    audio->AddCodec(AudioCodec(103, "ISAC", 16000, 0, 1));
    audio->AddCodec(AudioCodec(104, "ISAC", 32000, 0, 1));
    return audio;
  }

  // Turns the existing reference description into a unified plan description,
  // with 3 audio MediaContentDescriptions with special StreamParams that
  // contain 0 or multiple stream ids: - audio track 1 has 1 media stream id -
  // audio track 2 has 2 media stream ids - audio track 3 has 0 media stream ids
  void MakeUnifiedPlanDescriptionMultipleStreamIds() {
    desc_.RemoveContentByName(kVideoContentName);
    desc_.RemoveTransportInfoByName(kVideoContentName);
    RemoveVideoCandidates();

    // Audio track 2 has 2 media stream ids.
    AudioContentDescription* audio_desc_2 = CreateAudioContentDescription();
    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.cname = kStream1Cname;
    audio_track_2.set_stream_ids({kStreamId1, kStreamId2});
    audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio_desc_2->AddStream(audio_track_2);
    desc_.AddContent(kAudioContentName2, MediaProtocolType::kRtp, audio_desc_2);
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kAudioContentName2, TransportDescription(kUfragVoice2, kPwdVoice2))));

    // Audio track 3 has no stream ids.
    AudioContentDescription* audio_desc_3 = CreateAudioContentDescription();
    StreamParams audio_track_3;
    audio_track_3.id = kAudioTrackId3;
    audio_track_3.cname = kStream2Cname;
    audio_track_3.set_stream_ids({});
    audio_track_3.ssrcs.push_back(kAudioTrack3Ssrc);
    audio_desc_3->AddStream(audio_track_3);
    desc_.AddContent(kAudioContentName3, MediaProtocolType::kRtp, audio_desc_3);
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kAudioContentName3, TransportDescription(kUfragVoice3, kPwdVoice3))));
    // Make sure to create both a=msid lines.
    desc_.set_msid_signaling(cricket::kMsidSignalingMediaSection |
                             cricket::kMsidSignalingSsrcAttribute);
    ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(), jdesc_.session_id(),
                                  jdesc_.session_version()));
  }

  // Turns the existing reference description into a unified plan description
  // with one audio MediaContentDescription that contains one StreamParams with
  // 0 ssrcs.
  void MakeUnifiedPlanDescriptionNoSsrcSignaling() {
    desc_.RemoveContentByName(kVideoContentName);
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveTransportInfoByName(kVideoContentName);
    RemoveVideoCandidates();

    AudioContentDescription* audio_desc = CreateAudioContentDescription();
    StreamParams audio_track;
    audio_track.id = kAudioTrackId1;
    audio_track.set_stream_ids({kStreamId1});
    audio_desc->AddStream(audio_track);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_desc);

    // Enable signaling a=msid lines.
    desc_.set_msid_signaling(cricket::kMsidSignalingMediaSection);
    ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(), jdesc_.session_id(),
                                  jdesc_.session_version()));
  }

  // Creates a video content description with no streams, and some default
  // configuration.
  VideoContentDescription* CreateVideoContentDescription() {
    VideoContentDescription* video = new VideoContentDescription();
    video->AddCrypto(CryptoParams(
        1, "AES_CM_128_HMAC_SHA1_80",
        "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32", ""));
    video->set_protocol(cricket::kMediaProtocolSavpf);
    video->AddCodec(
        VideoCodec(120, JsepSessionDescription::kDefaultVideoCodecName));
    return video;
  }

  template <class MCD>
  void CompareMediaContentDescription(const MCD* cd1,
                                      const MCD* cd2) {
    // type
    EXPECT_EQ(cd1->type(), cd1->type());

    // content direction
    EXPECT_EQ(cd1->direction(), cd2->direction());

    // rtcp_mux
    EXPECT_EQ(cd1->rtcp_mux(), cd2->rtcp_mux());

    // rtcp_reduced_size
    EXPECT_EQ(cd1->rtcp_reduced_size(), cd2->rtcp_reduced_size());

    // cryptos
    EXPECT_EQ(cd1->cryptos().size(), cd2->cryptos().size());
    if (cd1->cryptos().size() != cd2->cryptos().size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i< cd1->cryptos().size(); ++i) {
      const CryptoParams c1 = cd1->cryptos().at(i);
      const CryptoParams c2 = cd2->cryptos().at(i);
      EXPECT_TRUE(c1.Matches(c2));
      EXPECT_EQ(c1.key_params, c2.key_params);
      EXPECT_EQ(c1.session_params, c2.session_params);
    }

    // protocol
    // Use an equivalence class here, for old and new versions of the
    // protocol description.
    if (cd1->protocol() == cricket::kMediaProtocolDtlsSctp
        || cd1->protocol() == cricket::kMediaProtocolUdpDtlsSctp
        || cd1->protocol() == cricket::kMediaProtocolTcpDtlsSctp) {
      const bool cd2_is_also_dtls_sctp =
        cd2->protocol() == cricket::kMediaProtocolDtlsSctp
        || cd2->protocol() == cricket::kMediaProtocolUdpDtlsSctp
        || cd2->protocol() == cricket::kMediaProtocolTcpDtlsSctp;
      EXPECT_TRUE(cd2_is_also_dtls_sctp);
    } else {
      EXPECT_EQ(cd1->protocol(), cd2->protocol());
    }

    // codecs
    EXPECT_EQ(cd1->codecs(), cd2->codecs());

    // bandwidth
    EXPECT_EQ(cd1->bandwidth(), cd2->bandwidth());

    // streams
    EXPECT_EQ(cd1->streams(), cd2->streams());

    // extmap
    ASSERT_EQ(cd1->rtp_header_extensions().size(),
              cd2->rtp_header_extensions().size());
    for (size_t i = 0; i< cd1->rtp_header_extensions().size(); ++i) {
      const RtpExtension ext1 = cd1->rtp_header_extensions().at(i);
      const RtpExtension ext2 = cd2->rtp_header_extensions().at(i);
      EXPECT_EQ(ext1.uri, ext2.uri);
      EXPECT_EQ(ext1.id, ext2.id);
      EXPECT_EQ(ext1.encrypt, ext2.encrypt);
    }
  }

  void CompareDataContentDescription(const DataContentDescription* dcd1,
                                     const DataContentDescription* dcd2) {
    EXPECT_EQ(dcd1->use_sctpmap(), dcd2->use_sctpmap());
    CompareMediaContentDescription<DataContentDescription>(dcd1, dcd2);
  }

  void CompareSessionDescription(const SessionDescription& desc1,
                                 const SessionDescription& desc2) {
    // Compare content descriptions.
    if (desc1.contents().size() != desc2.contents().size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0 ; i < desc1.contents().size(); ++i) {
      const cricket::ContentInfo& c1 = desc1.contents().at(i);
      const cricket::ContentInfo& c2 = desc2.contents().at(i);
      // ContentInfo properties.
      EXPECT_EQ(c1.name, c2.name);
      EXPECT_EQ(c1.type, c2.type);
      EXPECT_EQ(c1.rejected, c2.rejected);
      EXPECT_EQ(c1.bundle_only, c2.bundle_only);

      ASSERT_EQ(IsAudioContent(&c1), IsAudioContent(&c2));
      if (IsAudioContent(&c1)) {
        const AudioContentDescription* acd1 =
            c1.media_description()->as_audio();
        const AudioContentDescription* acd2 =
            c2.media_description()->as_audio();
        CompareMediaContentDescription<AudioContentDescription>(acd1, acd2);
      }

      ASSERT_EQ(IsVideoContent(&c1), IsVideoContent(&c2));
      if (IsVideoContent(&c1)) {
        const VideoContentDescription* vcd1 =
            c1.media_description()->as_video();
        const VideoContentDescription* vcd2 =
            c2.media_description()->as_video();
        CompareMediaContentDescription<VideoContentDescription>(vcd1, vcd2);
      }

      ASSERT_EQ(IsDataContent(&c1), IsDataContent(&c2));
      if (IsDataContent(&c1)) {
        const DataContentDescription* dcd1 = c1.media_description()->as_data();
        const DataContentDescription* dcd2 = c2.media_description()->as_data();
        CompareDataContentDescription(dcd1, dcd2);
      }
    }

    // group
    const cricket::ContentGroups groups1 = desc1.groups();
    const cricket::ContentGroups groups2 = desc2.groups();
    EXPECT_EQ(groups1.size(), groups1.size());
    if (groups1.size() != groups2.size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i < groups1.size(); ++i) {
      const cricket::ContentGroup group1 = groups1.at(i);
      const cricket::ContentGroup group2 = groups2.at(i);
      EXPECT_EQ(group1.semantics(), group2.semantics());
      const cricket::ContentNames names1 = group1.content_names();
      const cricket::ContentNames names2 = group2.content_names();
      EXPECT_EQ(names1.size(), names2.size());
      if (names1.size() != names2.size()) {
        ADD_FAILURE();
        return;
      }
      cricket::ContentNames::const_iterator iter1 = names1.begin();
      cricket::ContentNames::const_iterator iter2 = names2.begin();
      while (iter1 != names1.end()) {
        EXPECT_EQ(*iter1++, *iter2++);
      }
    }

    // transport info
    const cricket::TransportInfos transports1 = desc1.transport_infos();
    const cricket::TransportInfos transports2 = desc2.transport_infos();
    EXPECT_EQ(transports1.size(), transports2.size());
    if (transports1.size() != transports2.size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i < transports1.size(); ++i) {
      const cricket::TransportInfo transport1 = transports1.at(i);
      const cricket::TransportInfo transport2 = transports2.at(i);
      EXPECT_EQ(transport1.content_name, transport2.content_name);
      EXPECT_EQ(transport1.description.ice_ufrag,
                transport2.description.ice_ufrag);
      EXPECT_EQ(transport1.description.ice_pwd,
                transport2.description.ice_pwd);
      if (transport1.description.identity_fingerprint) {
        EXPECT_EQ(*transport1.description.identity_fingerprint,
                  *transport2.description.identity_fingerprint);
      } else {
        EXPECT_EQ(transport1.description.identity_fingerprint.get(),
                  transport2.description.identity_fingerprint.get());
      }
      EXPECT_EQ(transport1.description.transport_options,
                transport2.description.transport_options);
    }

    // global attributes
    EXPECT_EQ(desc1.msid_supported(), desc2.msid_supported());
  }

  bool CompareSessionDescription(
      const JsepSessionDescription& desc1,
      const JsepSessionDescription& desc2) {
    EXPECT_EQ(desc1.session_id(), desc2.session_id());
    EXPECT_EQ(desc1.session_version(), desc2.session_version());
    CompareSessionDescription(*desc1.description(), *desc2.description());
    if (desc1.number_of_mediasections() != desc2.number_of_mediasections())
      return false;
    for (size_t i = 0; i < desc1.number_of_mediasections(); ++i) {
      const IceCandidateCollection* cc1 = desc1.candidates(i);
      const IceCandidateCollection* cc2 = desc2.candidates(i);
      if (cc1->count() != cc2->count()) {
        ADD_FAILURE();
        return false;
      }
      for (size_t j = 0; j < cc1->count(); ++j) {
        const IceCandidateInterface* c1 = cc1->at(j);
        const IceCandidateInterface* c2 = cc2->at(j);
        EXPECT_EQ(c1->sdp_mid(), c2->sdp_mid());
        EXPECT_EQ(c1->sdp_mline_index(), c2->sdp_mline_index());
        EXPECT_TRUE(c1->candidate().IsEquivalent(c2->candidate()));
      }
    }
    return true;
  }

  // Disable the ice-ufrag and ice-pwd in given |sdp| message by replacing
  // them with invalid keywords so that the parser will just ignore them.
  bool RemoveCandidateUfragPwd(std::string* sdp) {
    const char ice_ufrag[] = "a=ice-ufrag";
    const char ice_ufragx[] = "a=xice-ufrag";
    const char ice_pwd[] = "a=ice-pwd";
    const char ice_pwdx[] = "a=xice-pwd";
    rtc::replace_substrs(ice_ufrag, strlen(ice_ufrag),
        ice_ufragx, strlen(ice_ufragx), sdp);
    rtc::replace_substrs(ice_pwd, strlen(ice_pwd),
        ice_pwdx, strlen(ice_pwdx), sdp);
    return true;
  }

  // Update the candidates in |jdesc| to use the given |ufrag| and |pwd|.
  bool UpdateCandidateUfragPwd(JsepSessionDescription* jdesc, int mline_index,
      const std::string& ufrag, const std::string& pwd) {
    std::string content_name;
    if (mline_index == 0) {
      content_name = kAudioContentName;
    } else if (mline_index == 1) {
      content_name = kVideoContentName;
    } else {
      RTC_NOTREACHED();
    }
    TransportInfo transport_info(
        content_name, TransportDescription(ufrag, pwd));
    SessionDescription* desc =
        const_cast<SessionDescription*>(jdesc->description());
    desc->RemoveTransportInfoByName(content_name);
    EXPECT_TRUE(desc->AddTransportInfo(transport_info));
    for (size_t i = 0; i < jdesc_.number_of_mediasections(); ++i) {
      const IceCandidateCollection* cc = jdesc_.candidates(i);
      for (size_t j = 0; j < cc->count(); ++j) {
        if (cc->at(j)->sdp_mline_index() == mline_index) {
          const_cast<Candidate&>(cc->at(j)->candidate()).set_username(
              ufrag);
          const_cast<Candidate&>(cc->at(j)->candidate()).set_password(
              pwd);
        }
      }
    }
    return true;
  }

  void AddIceOptions(const std::string& content_name,
                     const std::vector<std::string>& transport_options) {
    ASSERT_TRUE(desc_.GetTransportInfoByName(content_name) != NULL);
    cricket::TransportInfo transport_info =
        *(desc_.GetTransportInfoByName(content_name));
    desc_.RemoveTransportInfoByName(content_name);
    transport_info.description.transport_options = transport_options;
    desc_.AddTransportInfo(transport_info);
  }

  void SetIceUfragPwd(const std::string& content_name,
                      const std::string& ice_ufrag,
                      const std::string& ice_pwd) {
    ASSERT_TRUE(desc_.GetTransportInfoByName(content_name) != NULL);
    cricket::TransportInfo transport_info =
        *(desc_.GetTransportInfoByName(content_name));
    desc_.RemoveTransportInfoByName(content_name);
    transport_info.description.ice_ufrag = ice_ufrag;
    transport_info.description.ice_pwd = ice_pwd;
    desc_.AddTransportInfo(transport_info);
  }

  void AddFingerprint() {
    desc_.RemoveTransportInfoByName(kAudioContentName);
    desc_.RemoveTransportInfoByName(kVideoContentName);
    rtc::SSLFingerprint fingerprint(rtc::DIGEST_SHA_1,
                                          kIdentityDigest,
                                          sizeof(kIdentityDigest));
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kAudioContentName,
        TransportDescription(std::vector<std::string>(), kUfragVoice, kPwdVoice,
                             cricket::ICEMODE_FULL,
                             cricket::CONNECTIONROLE_NONE, &fingerprint))));
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kVideoContentName,
        TransportDescription(std::vector<std::string>(), kUfragVideo, kPwdVideo,
                             cricket::ICEMODE_FULL,
                             cricket::CONNECTIONROLE_NONE, &fingerprint))));
  }

  void AddExtmap(bool encrypted) {
    audio_desc_ = audio_desc_->Copy();
    video_desc_ = video_desc_->Copy();
    audio_desc_->AddRtpHeaderExtension(
        RtpExtension(kExtmapUri, kExtmapId, encrypted));
    video_desc_->AddRtpHeaderExtension(
        RtpExtension(kExtmapUri, kExtmapId, encrypted));
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_desc_);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_desc_);
  }

  void RemoveCryptos() {
    audio_desc_->set_cryptos(std::vector<CryptoParams>());
    video_desc_->set_cryptos(std::vector<CryptoParams>());
  }

  // Removes everything in StreamParams from the session description that is
  // used for a=ssrc lines.
  void RemoveSsrcSignalingFromStreamParams() {
    for (cricket::ContentInfo content_info : jdesc_.description()->contents()) {
      // With Unified Plan there should be one StreamParams per m= section.
      StreamParams& stream =
          content_info.media_description()->mutable_streams()[0];
      stream.ssrcs.clear();
      stream.ssrc_groups.clear();
      stream.cname.clear();
    }
  }

  // Removes all a=ssrc lines from the SDP string.
  void RemoveSsrcLinesFromSdpString(std::string* sdp_string) {
    const char kAttributeSsrc[] = "a=ssrc";
    while (sdp_string->find(kAttributeSsrc) != std::string::npos) {
      size_t pos_ssrc_attribute = sdp_string->find(kAttributeSsrc);
      size_t beg_line_pos = sdp_string->rfind('\n', pos_ssrc_attribute);
      size_t end_line_pos = sdp_string->find('\n', pos_ssrc_attribute);
      sdp_string->erase(beg_line_pos, end_line_pos - beg_line_pos);
    }
  }

  bool TestSerializeDirection(RtpTransceiverDirection direction) {
    audio_desc_->set_direction(direction);
    video_desc_->set_direction(direction);
    std::string new_sdp = kSdpFullString;
    ReplaceDirection(direction, &new_sdp);

    if (!jdesc_.Initialize(desc_.Copy(),
                           jdesc_.session_id(),
                           jdesc_.session_version())) {
      return false;
    }
    std::string message = webrtc::SdpSerialize(jdesc_);
    EXPECT_EQ(new_sdp, message);
    return true;
  }

  bool TestSerializeRejected(bool audio_rejected, bool video_rejected) {
    audio_desc_ = audio_desc_->Copy();
    video_desc_ = video_desc_->Copy();

    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_rejected,
                     audio_desc_);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_rejected,
                     video_desc_);
    SetIceUfragPwd(kAudioContentName, audio_rejected ? "" : kUfragVoice,
                   audio_rejected ? "" : kPwdVoice);
    SetIceUfragPwd(kVideoContentName, video_rejected ? "" : kUfragVideo,
                   video_rejected ? "" : kPwdVideo);

    std::string new_sdp = kSdpString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);

    JsepSessionDescription jdesc_no_candidates(kDummyType);
    MakeDescriptionWithoutCandidates(&jdesc_no_candidates);
    std::string message = webrtc::SdpSerialize(jdesc_no_candidates);
    EXPECT_EQ(new_sdp, message);
    return true;
  }

  void AddSctpDataChannel(bool use_sctpmap) {
    std::unique_ptr<DataContentDescription> data(new DataContentDescription());
    data_desc_ = data.get();
    data_desc_->set_use_sctpmap(use_sctpmap);
    data_desc_->set_protocol(cricket::kMediaProtocolDtlsSctp);
    DataCodec codec(cricket::kGoogleSctpDataCodecPlType,
                    cricket::kGoogleSctpDataCodecName);
    codec.SetParam(cricket::kCodecParamPort, kDefaultSctpPort);
    data_desc_->AddCodec(codec);
    desc_.AddContent(kDataContentName, MediaProtocolType::kSctp,
                     data.release());
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kDataContentName, TransportDescription(kUfragData, kPwdData))));
  }

  void AddRtpDataChannel() {
    std::unique_ptr<DataContentDescription> data(new DataContentDescription());
    data_desc_ = data.get();

    data_desc_->AddCodec(DataCodec(101, "google-data"));
    StreamParams data_stream;
    data_stream.id = kDataChannelMsid;
    data_stream.cname = kDataChannelCname;
    data_stream.set_stream_ids({kDataChannelLabel});
    data_stream.ssrcs.push_back(kDataChannelSsrc);
    data_desc_->AddStream(data_stream);
    data_desc_->AddCrypto(CryptoParams(
        1, "AES_CM_128_HMAC_SHA1_80",
        "inline:FvLcvU2P3ZWmQxgPAgcDu7Zl9vftYElFOjEzhWs5", ""));
    data_desc_->set_protocol(cricket::kMediaProtocolSavpf);
    desc_.AddContent(kDataContentName, MediaProtocolType::kRtp, data.release());
    EXPECT_TRUE(desc_.AddTransportInfo(TransportInfo(
        kDataContentName, TransportDescription(kUfragData, kPwdData))));
  }

  bool TestDeserializeDirection(RtpTransceiverDirection direction) {
    std::string new_sdp = kSdpFullString;
    ReplaceDirection(direction, &new_sdp);
    JsepSessionDescription new_jdesc(kDummyType);

    EXPECT_TRUE(SdpDeserialize(new_sdp, &new_jdesc));

    audio_desc_->set_direction(direction);
    video_desc_->set_direction(direction);
    if (!jdesc_.Initialize(desc_.Copy(),
                           jdesc_.session_id(),
                           jdesc_.session_version())) {
      return false;
    }
    EXPECT_TRUE(CompareSessionDescription(jdesc_, new_jdesc));
    return true;
  }

  bool TestDeserializeRejected(bool audio_rejected, bool video_rejected) {
    std::string new_sdp = kSdpString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);
    JsepSessionDescription new_jdesc(SdpType::kOffer);
    EXPECT_TRUE(SdpDeserialize(new_sdp, &new_jdesc));

    audio_desc_ = audio_desc_->Copy();
    video_desc_ = video_desc_->Copy();
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_rejected,
                     audio_desc_);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_rejected,
                     video_desc_);
    SetIceUfragPwd(kAudioContentName, audio_rejected ? "" : kUfragVoice,
                   audio_rejected ? "" : kPwdVoice);
    SetIceUfragPwd(kVideoContentName, video_rejected ? "" : kUfragVideo,
                   video_rejected ? "" : kPwdVideo);
    JsepSessionDescription jdesc_no_candidates(kDummyType);
    if (!jdesc_no_candidates.Initialize(desc_.Copy(), jdesc_.session_id(),
                                        jdesc_.session_version())) {
      return false;
    }
    EXPECT_TRUE(CompareSessionDescription(jdesc_no_candidates, new_jdesc));
    return true;
  }

  void TestDeserializeExtmap(bool session_level, bool media_level,
      bool encrypted) {
    AddExtmap(encrypted);
    JsepSessionDescription new_jdesc(SdpType::kOffer);
    ASSERT_TRUE(new_jdesc.Initialize(desc_.Copy(),
                                     jdesc_.session_id(),
                                     jdesc_.session_version()));
    JsepSessionDescription jdesc_with_extmap(SdpType::kOffer);
    std::string sdp_with_extmap = kSdpString;
    if (session_level) {
      InjectAfter(kSessionTime,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
    }
    if (media_level) {
      InjectAfter(kAttributeIcePwdVoice,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
      InjectAfter(kAttributeIcePwdVideo,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
    }
    // The extmap can't be present at the same time in both session level and
    // media level.
    if (session_level && media_level) {
      SdpParseError error;
      EXPECT_FALSE(webrtc::SdpDeserialize(sdp_with_extmap,
                   &jdesc_with_extmap, &error));
      EXPECT_NE(std::string::npos, error.description.find("a=extmap"));
    } else {
      EXPECT_TRUE(SdpDeserialize(sdp_with_extmap, &jdesc_with_extmap));
      EXPECT_TRUE(CompareSessionDescription(jdesc_with_extmap, new_jdesc));
    }
  }

  void VerifyCodecParameter(const cricket::CodecParameterMap& params,
      const std::string& name, int expected_value) {
    cricket::CodecParameterMap::const_iterator found = params.find(name);
    ASSERT_TRUE(found != params.end());
    EXPECT_EQ(found->second, rtc::ToString<int>(expected_value));
  }

  void TestDeserializeCodecParams(const CodecParams& params,
                                  JsepSessionDescription* jdesc_output) {
    std::string sdp =
        "v=0\r\n"
        "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        // Include semantics for WebRTC Media Streams since it is supported by
        // this parser, and will be added to the SDP when serializing a session
        // description.
        "a=msid-semantic: WMS\r\n"
        // Pl type 111 preferred.
        "m=audio 9 RTP/SAVPF 111 104 103\r\n"
        // Pltype 111 listed before 103 and 104 in the map.
        "a=rtpmap:111 opus/48000/2\r\n"
        // Pltype 103 listed before 104.
        "a=rtpmap:103 ISAC/16000\r\n"
        "a=rtpmap:104 ISAC/32000\r\n"
        "a=fmtp:111 0-15,66,70\r\n"
        "a=fmtp:111 ";
    std::ostringstream os;
    os << "minptime=" << params.min_ptime << "; stereo=" << params.stereo
       << "; sprop-stereo=" << params.sprop_stereo
       << "; useinbandfec=" << params.useinband
       << "; maxaveragebitrate=" << params.maxaveragebitrate << "\r\n"
       << "a=ptime:" << params.ptime << "\r\n"
       << "a=maxptime:" << params.max_ptime << "\r\n";
    sdp += os.str();

    os.clear();
    os.str("");
    // Pl type 100 preferred.
    os << "m=video 9 RTP/SAVPF 99 95\r\n"
       << "a=rtpmap:99 VP8/90000\r\n"
       << "a=rtpmap:95 RTX/90000\r\n"
       << "a=fmtp:95 apt=99;\r\n";
    sdp += os.str();

    // Deserialize
    SdpParseError error;
    EXPECT_TRUE(webrtc::SdpDeserialize(sdp, jdesc_output, &error));

    const AudioContentDescription* acd =
        GetFirstAudioContentDescription(jdesc_output->description());
    ASSERT_TRUE(acd);
    ASSERT_FALSE(acd->codecs().empty());
    cricket::AudioCodec opus = acd->codecs()[0];
    EXPECT_EQ("opus", opus.name);
    EXPECT_EQ(111, opus.id);
    VerifyCodecParameter(opus.params, "minptime", params.min_ptime);
    VerifyCodecParameter(opus.params, "stereo", params.stereo);
    VerifyCodecParameter(opus.params, "sprop-stereo", params.sprop_stereo);
    VerifyCodecParameter(opus.params, "useinbandfec", params.useinband);
    VerifyCodecParameter(opus.params, "maxaveragebitrate",
                         params.maxaveragebitrate);
    for (size_t i = 0; i < acd->codecs().size(); ++i) {
      cricket::AudioCodec codec = acd->codecs()[i];
      VerifyCodecParameter(codec.params, "ptime", params.ptime);
      VerifyCodecParameter(codec.params, "maxptime", params.max_ptime);
    }

    const VideoContentDescription* vcd =
        GetFirstVideoContentDescription(jdesc_output->description());
    ASSERT_TRUE(vcd);
    ASSERT_FALSE(vcd->codecs().empty());
    cricket::VideoCodec vp8 = vcd->codecs()[0];
    EXPECT_EQ("VP8", vp8.name);
    EXPECT_EQ(99, vp8.id);
    cricket::VideoCodec rtx = vcd->codecs()[1];
    EXPECT_EQ("RTX", rtx.name);
    EXPECT_EQ(95, rtx.id);
    VerifyCodecParameter(rtx.params, "apt", vp8.id);
  }

  void TestDeserializeRtcpFb(JsepSessionDescription* jdesc_output,
                             bool use_wildcard) {
    std::string sdp_session_and_audio =
        "v=0\r\n"
        "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        // Include semantics for WebRTC Media Streams since it is supported by
        // this parser, and will be added to the SDP when serializing a session
        // description.
        "a=msid-semantic: WMS\r\n"
        "m=audio 9 RTP/SAVPF 111\r\n"
        "a=rtpmap:111 opus/48000/2\r\n";
    std::string sdp_video =
        "m=video 3457 RTP/SAVPF 101\r\n"
        "a=rtpmap:101 VP8/90000\r\n"
        "a=rtcp-fb:101 nack\r\n"
        "a=rtcp-fb:101 nack pli\r\n"
        "a=rtcp-fb:101 goog-remb\r\n";
    std::ostringstream os;
    os << sdp_session_and_audio;
    os << "a=rtcp-fb:" << (use_wildcard ? "*" : "111") <<  " nack\r\n";
    os << sdp_video;
    os << "a=rtcp-fb:" << (use_wildcard ? "*" : "101") <<  " ccm fir\r\n";
    std::string sdp = os.str();
    // Deserialize
    SdpParseError error;
    EXPECT_TRUE(webrtc::SdpDeserialize(sdp, jdesc_output, &error));
    const AudioContentDescription* acd =
        GetFirstAudioContentDescription(jdesc_output->description());
    ASSERT_TRUE(acd);
    ASSERT_FALSE(acd->codecs().empty());
    cricket::AudioCodec opus = acd->codecs()[0];
    EXPECT_EQ(111, opus.id);
    EXPECT_TRUE(opus.HasFeedbackParam(
        cricket::FeedbackParam(cricket::kRtcpFbParamNack,
                               cricket::kParamValueEmpty)));

    const VideoContentDescription* vcd =
        GetFirstVideoContentDescription(jdesc_output->description());
    ASSERT_TRUE(vcd);
    ASSERT_FALSE(vcd->codecs().empty());
    cricket::VideoCodec vp8 = vcd->codecs()[0];
    EXPECT_STREQ(webrtc::JsepSessionDescription::kDefaultVideoCodecName,
                 vp8.name.c_str());
    EXPECT_EQ(101, vp8.id);
    EXPECT_TRUE(vp8.HasFeedbackParam(
        cricket::FeedbackParam(cricket::kRtcpFbParamNack,
                               cricket::kParamValueEmpty)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        cricket::FeedbackParam(cricket::kRtcpFbParamNack,
                               cricket::kRtcpFbNackParamPli)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        cricket::FeedbackParam(cricket::kRtcpFbParamRemb,
                               cricket::kParamValueEmpty)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        cricket::FeedbackParam(cricket::kRtcpFbParamCcm,
                               cricket::kRtcpFbCcmParamFir)));
  }

  // Two SDP messages can mean the same thing but be different strings, e.g.
  // some of the lines can be serialized in different order.
  // However, a deserialized description can be compared field by field and has
  // no order. If deserializer has already been tested, serializing then
  // deserializing and comparing JsepSessionDescription will test
  // the serializer sufficiently.
  void TestSerialize(const JsepSessionDescription& jdesc) {
    std::string message = webrtc::SdpSerialize(jdesc);
    JsepSessionDescription jdesc_output_des(kDummyType);
    SdpParseError error;
    EXPECT_TRUE(webrtc::SdpDeserialize(message, &jdesc_output_des, &error));
    EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output_des));
  }

  // Calling 'Initialize' with a copy of the inner SessionDescription will
  // create a copy of the JsepSessionDescription without candidates. The
  // 'connection address' field, previously set from the candidates, must also
  // be reset.
  void MakeDescriptionWithoutCandidates(JsepSessionDescription* jdesc) {
    rtc::SocketAddress audio_addr("0.0.0.0", 9);
    rtc::SocketAddress video_addr("0.0.0.0", 9);
    audio_desc_->set_connection_address(audio_addr);
    video_desc_->set_connection_address(video_addr);
    ASSERT_TRUE(jdesc->Initialize(desc_.Copy(), kSessionId, kSessionVersion));
  }

 protected:
  SessionDescription desc_;
  AudioContentDescription* audio_desc_;
  VideoContentDescription* video_desc_;
  DataContentDescription* data_desc_;
  Candidates candidates_;
  std::unique_ptr<IceCandidateInterface> jcandidate_;
  JsepSessionDescription jdesc_;
};

void TestMismatch(const std::string& string1, const std::string& string2) {
  int position = 0;
  for (size_t i = 0; i < string1.length() && i < string2.length(); ++i) {
    if (string1.c_str()[i] != string2.c_str()[i]) {
      position = static_cast<int>(i);
      break;
    }
  }
  EXPECT_EQ(0, position) << "Strings mismatch at the " << position
                         << " character\n"
                         << " 1: " << string1.substr(position, 20) << "\n"
                         << " 2: " << string2.substr(position, 20) << "\n";
}

TEST_F(WebRtcSdpTest, SerializeSessionDescription) {
  // SessionDescription with desc and candidates.
  std::string message = webrtc::SdpSerialize(jdesc_);
  TestMismatch(std::string(kSdpFullString), message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionEmpty) {
  JsepSessionDescription jdesc_empty(kDummyType);
  EXPECT_EQ("", webrtc::SdpSerialize(jdesc_empty));
}

// This tests serialization of SDP with a=crypto and a=fingerprint, as would be
// the case in a DTLS offer.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithFingerprint) {
  AddFingerprint();
  JsepSessionDescription jdesc_with_fingerprint(kDummyType);
  MakeDescriptionWithoutCandidates(&jdesc_with_fingerprint);
  std::string message = webrtc::SdpSerialize(jdesc_with_fingerprint);

  std::string sdp_with_fingerprint = kSdpString;
  InjectAfter(kAttributeIcePwdVoice,
              kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo,
              kFingerprint, &sdp_with_fingerprint);

  EXPECT_EQ(sdp_with_fingerprint, message);
}

// This tests serialization of SDP with a=fingerprint with no a=crypto, as would
// be the case in a DTLS answer.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithFingerprintNoCryptos) {
  AddFingerprint();
  RemoveCryptos();
  JsepSessionDescription jdesc_with_fingerprint(kDummyType);
  MakeDescriptionWithoutCandidates(&jdesc_with_fingerprint);
  std::string message = webrtc::SdpSerialize(jdesc_with_fingerprint);

  std::string sdp_with_fingerprint = kSdpString;
  Replace(kAttributeCryptoVoice, "", &sdp_with_fingerprint);
  Replace(kAttributeCryptoVideo, "", &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVoice,
              kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo,
              kFingerprint, &sdp_with_fingerprint);

  EXPECT_EQ(sdp_with_fingerprint, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithoutCandidates) {
  // JsepSessionDescription with desc but without candidates.
  JsepSessionDescription jdesc_no_candidates(kDummyType);
  MakeDescriptionWithoutCandidates(&jdesc_no_candidates);
  std::string message = webrtc::SdpSerialize(jdesc_no_candidates);
  EXPECT_EQ(std::string(kSdpString), message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithBundle) {
  ContentGroup group(cricket::GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioContentName);
  group.AddContentName(kVideoContentName);
  desc_.AddGroup(group);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=group:BUNDLE audio_content_name video_content_name\r\n",
              &sdp_with_bundle);
  EXPECT_EQ(sdp_with_bundle, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithBandwidth) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000);
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(50 * 1000);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("c=IN IP4 74.125.224.39\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  InjectAfter("c=IN IP4 74.125.127.126\r\n",
              "b=AS:50\r\n",
              &sdp_with_bandwidth);
  EXPECT_EQ(sdp_with_bandwidth, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithIceOptions) {
  std::vector<std::string> transport_options;
  transport_options.push_back(kIceOption1);
  transport_options.push_back(kIceOption3);
  AddIceOptions(kAudioContentName, transport_options);
  transport_options.clear();
  transport_options.push_back(kIceOption2);
  transport_options.push_back(kIceOption3);
  AddIceOptions(kVideoContentName, transport_options);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_ice_options = kSdpFullString;
  InjectAfter(kAttributeIcePwdVoice,
              "a=ice-options:iceoption1 iceoption3\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVideo,
              "a=ice-options:iceoption2 iceoption3\r\n",
              &sdp_with_ice_options);
  EXPECT_EQ(sdp_with_ice_options, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kRecvOnly));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kSendOnly));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kInactive));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithAudioRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithAudioVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, true));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithRtpDataChannel) {
  AddRtpDataChannel();
  JsepSessionDescription jsep_desc(kDummyType);

  MakeDescriptionWithoutCandidates(&jsep_desc);
  std::string message = webrtc::SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpRtpDataChannelString);
  EXPECT_EQ(expected_sdp, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithSctpDataChannel) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jsep_desc(kDummyType);

  MakeDescriptionWithoutCandidates(&jsep_desc);
  std::string message = webrtc::SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpSctpDataChannelString);
  EXPECT_EQ(message, expected_sdp);
}

TEST_F(WebRtcSdpTest, SerializeWithSctpDataChannelAndNewPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jsep_desc(kDummyType);
  MakeDescriptionWithoutCandidates(&jsep_desc);
  DataContentDescription* dcdesc =
      jsep_desc.description()
          ->GetContentDescriptionByName(kDataContentName)
          ->as_data();

  const int kNewPort = 1234;
  cricket::DataCodec codec(cricket::kGoogleSctpDataCodecPlType,
                           cricket::kGoogleSctpDataCodecName);
  codec.SetParam(cricket::kCodecParamPort, kNewPort);
  dcdesc->AddOrReplaceCodec(codec);

  std::string message = webrtc::SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpSctpDataChannelString);

  char default_portstr[16];
  char new_portstr[16];
  rtc::sprintfn(default_portstr, sizeof(default_portstr), "%d",
                      kDefaultSctpPort);
  rtc::sprintfn(new_portstr, sizeof(new_portstr), "%d", kNewPort);
  rtc::replace_substrs(default_portstr, strlen(default_portstr),
                             new_portstr, strlen(new_portstr),
                             &expected_sdp);

  EXPECT_EQ(expected_sdp, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithDataChannelAndBandwidth) {
  JsepSessionDescription jsep_desc(kDummyType);
  AddRtpDataChannel();
  data_desc_->set_bandwidth(100*1000);
  MakeDescriptionWithoutCandidates(&jsep_desc);
  std::string message = webrtc::SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpRtpDataChannelString);
  // Serializing data content shouldn't ignore bandwidth settings.
  InjectAfter("m=application 9 RTP/SAVPF 101\r\nc=IN IP4 0.0.0.0\r\n",
              "b=AS:100\r\n",
              &expected_sdp);
  EXPECT_EQ(expected_sdp, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithExtmap) {
  bool encrypted = false;
  AddExtmap(encrypted);
  JsepSessionDescription desc_with_extmap(kDummyType);
  MakeDescriptionWithoutCandidates(&desc_with_extmap);
  std::string message = webrtc::SdpSerialize(desc_with_extmap);

  std::string sdp_with_extmap = kSdpString;
  InjectAfter("a=mid:audio_content_name\r\n",
              kExtmap, &sdp_with_extmap);
  InjectAfter("a=mid:video_content_name\r\n",
              kExtmap, &sdp_with_extmap);

  EXPECT_EQ(sdp_with_extmap, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithExtmapEncrypted) {
  bool encrypted = true;
  AddExtmap(encrypted);
  JsepSessionDescription desc_with_extmap(kDummyType);
  ASSERT_TRUE(desc_with_extmap.Initialize(desc_.Copy(),
                                          kSessionId, kSessionVersion));
  TestSerialize(desc_with_extmap);
}

TEST_F(WebRtcSdpTest, SerializeCandidates) {
  std::string message = webrtc::SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate), message);

  Candidate candidate_with_ufrag(candidates_.front());
  candidate_with_ufrag.set_username("ABC");
  jcandidate_.reset(new JsepIceCandidate(std::string("audio_content_name"), 0,
                                         candidate_with_ufrag));
  message = webrtc::SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " ufrag ABC", message);

  Candidate candidate_with_network_info(candidates_.front());
  candidate_with_network_info.set_network_id(1);
  jcandidate_.reset(new JsepIceCandidate(std::string("audio"), 0,
                                         candidate_with_network_info));
  message = webrtc::SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " network-id 1", message);
  candidate_with_network_info.set_network_cost(999);
  jcandidate_.reset(new JsepIceCandidate(std::string("audio"), 0,
                                         candidate_with_network_info));
  message = webrtc::SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " network-id 1 network-cost 999",
            message);
}

// TODO(mallinath) : Enable this test once WebRTCSdp capable of parsing
// RFC 6544.
TEST_F(WebRtcSdpTest, SerializeTcpCandidates) {
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                      rtc::SocketAddress("192.168.1.5", 9), kCandidatePriority,
                      "", "", LOCAL_PORT_TYPE, kCandidateGeneration,
                      kCandidateFoundation1);
  candidate.set_tcptype(cricket::TCPTYPE_ACTIVE_STR);
  std::unique_ptr<IceCandidateInterface> jcandidate(
      new JsepIceCandidate(std::string("audio_content_name"), 0, candidate));

  std::string message = webrtc::SdpSerializeCandidate(*jcandidate);
  EXPECT_EQ(std::string(kSdpTcpActiveCandidate), message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithH264) {
  cricket::VideoCodec h264_codec("H264");
  h264_codec.SetParam("profile-level-id", "42e01f");
  h264_codec.SetParam("level-asymmetry-allowed", "1");
  h264_codec.SetParam("packetization-mode", "1");
  video_desc_->AddCodec(h264_codec);

  jdesc_.Initialize(desc_.Copy(), kSessionId, kSessionVersion);

  std::string message = webrtc::SdpSerialize(jdesc_);
  size_t after_pt = message.find(" H264/90000");
  ASSERT_NE(after_pt, std::string::npos);
  size_t before_pt = message.rfind("a=rtpmap:", after_pt);
  ASSERT_NE(before_pt, std::string::npos);
  before_pt += strlen("a=rtpmap:");
  std::string pt = message.substr(before_pt, after_pt - before_pt);
  // TODO(hta): Check if payload type |pt| occurs in the m=video line.
  std::string to_find = "a=fmtp:" + pt + " ";
  size_t fmtp_pos = message.find(to_find);
  ASSERT_NE(std::string::npos, fmtp_pos) << "Failed to find " << to_find;
  size_t fmtp_endpos = message.find("\n", fmtp_pos);
  ASSERT_NE(std::string::npos, fmtp_endpos);
  std::string fmtp_value = message.substr(fmtp_pos, fmtp_endpos);
  EXPECT_NE(std::string::npos, fmtp_value.find("level-asymmetry-allowed=1"));
  EXPECT_NE(std::string::npos, fmtp_value.find("packetization-mode=1"));
  EXPECT_NE(std::string::npos, fmtp_value.find("profile-level-id=42e01f"));
  // Check that there are no spaces after semicolons.
  // https://bugs.webrtc.org/5793
  EXPECT_EQ(std::string::npos, fmtp_value.find("; "));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescription) {
  JsepSessionDescription jdesc(kDummyType);
  // Deserialize
  EXPECT_TRUE(SdpDeserialize(kSdpFullString, &jdesc));
  // Verify
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutMline) {
  JsepSessionDescription jdesc(kDummyType);
  const char kSdpWithoutMline[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1 local_stream_2\r\n";
  // Deserialize
  EXPECT_TRUE(SdpDeserialize(kSdpWithoutMline, &jdesc));
  EXPECT_EQ(0u, jdesc.description()->contents().size());
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutCarriageReturn) {
  JsepSessionDescription jdesc(kDummyType);
  std::string sdp_without_carriage_return = kSdpFullString;
  Replace("\r\n", "\n", &sdp_without_carriage_return);
  // Deserialize
  EXPECT_TRUE(SdpDeserialize(sdp_without_carriage_return, &jdesc));
  // Verify
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutCandidates) {
  // SessionDescription with desc but without candidates.
  JsepSessionDescription jdesc_no_candidates(kDummyType);
  ASSERT_TRUE(jdesc_no_candidates.Initialize(desc_.Copy(),
                                             kSessionId, kSessionVersion));
  JsepSessionDescription new_jdesc(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kSdpString, &new_jdesc));
  EXPECT_TRUE(CompareSessionDescription(jdesc_no_candidates, new_jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutRtpmap) {
  static const char kSdpNoRtpmapString[] =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 0 18 103\r\n"
      // Codec that doesn't appear in the m= line will be ignored.
      "a=rtpmap:104 ISAC/32000\r\n"
      // The rtpmap line for static payload codec is optional.
      "a=rtpmap:18 G729/16000\r\n"
      "a=rtpmap:103 ISAC/16000\r\n";

  JsepSessionDescription jdesc(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kSdpNoRtpmapString, &jdesc));
  cricket::AudioContentDescription* audio =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_AUDIO)
          ->as_audio();
  AudioCodecs ref_codecs;
  // The codecs in the AudioContentDescription should be in the same order as
  // the payload types (<fmt>s) on the m= line.
  ref_codecs.push_back(AudioCodec(0, "PCMU", 8000, 0, 1));
  ref_codecs.push_back(AudioCodec(18, "G729", 16000, 0, 1));
  ref_codecs.push_back(AudioCodec(103, "ISAC", 16000, 0, 1));
  EXPECT_EQ(ref_codecs, audio->codecs());
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutRtpmapButWithFmtp) {
  static const char kSdpNoRtpmapString[] =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 18 103\r\n"
      "a=fmtp:18 annexb=yes\r\n"
      "a=rtpmap:103 ISAC/16000\r\n";

  JsepSessionDescription jdesc(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kSdpNoRtpmapString, &jdesc));
  cricket::AudioContentDescription* audio =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_AUDIO)
          ->as_audio();

  cricket::AudioCodec g729 = audio->codecs()[0];
  EXPECT_EQ("G729", g729.name);
  EXPECT_EQ(8000, g729.clockrate);
  EXPECT_EQ(18, g729.id);
  cricket::CodecParameterMap::iterator found =
      g729.params.find("annexb");
  ASSERT_TRUE(found != g729.params.end());
  EXPECT_EQ(found->second, "yes");

  cricket::AudioCodec isac = audio->codecs()[1];
  EXPECT_EQ("ISAC", isac.name);
  EXPECT_EQ(103, isac.id);
  EXPECT_EQ(16000, isac.clockrate);
}

// Ensure that we can deserialize SDP with a=fingerprint properly.
TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithFingerprint) {
  // Add a DTLS a=fingerprint attribute to our session description.
  AddFingerprint();
  JsepSessionDescription new_jdesc(kDummyType);
  ASSERT_TRUE(new_jdesc.Initialize(desc_.Copy(),
                                   jdesc_.session_id(),
                                   jdesc_.session_version()));

  JsepSessionDescription jdesc_with_fingerprint(kDummyType);
  std::string sdp_with_fingerprint = kSdpString;
  InjectAfter(kAttributeIcePwdVoice, kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo, kFingerprint, &sdp_with_fingerprint);
  EXPECT_TRUE(SdpDeserialize(sdp_with_fingerprint, &jdesc_with_fingerprint));
  EXPECT_TRUE(CompareSessionDescription(jdesc_with_fingerprint, new_jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithBundle) {
  JsepSessionDescription jdesc_with_bundle(kDummyType);
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=group:BUNDLE audio_content_name video_content_name\r\n",
              &sdp_with_bundle);
  EXPECT_TRUE(SdpDeserialize(sdp_with_bundle, &jdesc_with_bundle));
  ContentGroup group(cricket::GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioContentName);
  group.AddContentName(kVideoContentName);
  desc_.AddGroup(group);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc_with_bundle));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithBandwidth) {
  JsepSessionDescription jdesc_with_bandwidth(kDummyType);
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  InjectAfter("a=mid:audio_content_name\r\na=sendrecv\r\n",
              "b=AS:50\r\n",
              &sdp_with_bandwidth);
  EXPECT_TRUE(
      SdpDeserialize(sdp_with_bandwidth, &jdesc_with_bandwidth));
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000);
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(50 * 1000);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithIceOptions) {
  JsepSessionDescription jdesc_with_ice_options(kDummyType);
  std::string sdp_with_ice_options = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=ice-options:iceoption3\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVoice,
              "a=ice-options:iceoption1\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVideo,
              "a=ice-options:iceoption2\r\n",
              &sdp_with_ice_options);
  EXPECT_TRUE(SdpDeserialize(sdp_with_ice_options, &jdesc_with_ice_options));
  std::vector<std::string> transport_options;
  transport_options.push_back(kIceOption3);
  transport_options.push_back(kIceOption1);
  AddIceOptions(kAudioContentName, transport_options);
  transport_options.clear();
  transport_options.push_back(kIceOption3);
  transport_options.push_back(kIceOption2);
  AddIceOptions(kVideoContentName, transport_options);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc_with_ice_options));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithUfragPwd) {
  // Remove the original ice-ufrag and ice-pwd
  JsepSessionDescription jdesc_with_ufrag_pwd(kDummyType);
  std::string sdp_with_ufrag_pwd = kSdpFullString;
  EXPECT_TRUE(RemoveCandidateUfragPwd(&sdp_with_ufrag_pwd));
  // Add session level ufrag and pwd
  InjectAfter(kSessionTime,
      "a=ice-pwd:session+level+icepwd\r\n"
      "a=ice-ufrag:session+level+iceufrag\r\n",
      &sdp_with_ufrag_pwd);
  // Add media level ufrag and pwd for audio
  InjectAfter("a=mid:audio_content_name\r\n",
      "a=ice-pwd:media+level+icepwd\r\na=ice-ufrag:media+level+iceufrag\r\n",
      &sdp_with_ufrag_pwd);
  // Update the candidate ufrag and pwd to the expected ones.
  EXPECT_TRUE(UpdateCandidateUfragPwd(&jdesc_, 0,
      "media+level+iceufrag", "media+level+icepwd"));
  EXPECT_TRUE(UpdateCandidateUfragPwd(&jdesc_, 1,
      "session+level+iceufrag", "session+level+icepwd"));
  EXPECT_TRUE(SdpDeserialize(sdp_with_ufrag_pwd, &jdesc_with_ufrag_pwd));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc_with_ufrag_pwd));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kRecvOnly));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kSendOnly));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kInactive));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedAudio) {
  EXPECT_TRUE(TestDeserializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedVideo) {
  EXPECT_TRUE(TestDeserializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedAudioVideo) {
  EXPECT_TRUE(TestDeserializeRejected(true, true));
}

// Tests that we can still handle the sdp uses mslabel and label instead of
// msid for backward compatibility.
TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutMsid) {
  jdesc_.description()->set_msid_supported(false);
  JsepSessionDescription jdesc(kDummyType);
  std::string sdp_without_msid = kSdpFullString;
  Replace("msid", "xmsid", &sdp_without_msid);
  // Deserialize
  EXPECT_TRUE(SdpDeserialize(sdp_without_msid, &jdesc));
  // Verify
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeCandidate) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);

  std::string sdp = kSdpOneCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(jcandidate_->candidate()));
  EXPECT_EQ(0, jcandidate.candidate().network_cost());

  // Candidate line without generation extension.
  sdp = kSdpOneCandidate;
  Replace(" generation 2", "", &sdp);
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  Candidate expected = jcandidate_->candidate();
  expected.set_generation(0);
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(expected));

  // Candidate with network id and/or cost.
  sdp = kSdpOneCandidate;
  Replace(" generation 2", " generation 2 network-id 2", &sdp);
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  expected = jcandidate_->candidate();
  expected.set_network_id(2);
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(expected));
  EXPECT_EQ(0, jcandidate.candidate().network_cost());
  // Add network cost
  Replace(" network-id 2", " network-id 2 network-cost 9", &sdp);
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(expected));
  EXPECT_EQ(9, jcandidate.candidate().network_cost());

  sdp = kSdpTcpActiveCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  // Make a cricket::Candidate equivalent to kSdpTcpCandidate string.
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                      rtc::SocketAddress("192.168.1.5", 9), kCandidatePriority,
                      "", "", LOCAL_PORT_TYPE, kCandidateGeneration,
                      kCandidateFoundation1);
  std::unique_ptr<IceCandidateInterface> jcandidate_template(
      new JsepIceCandidate(std::string("audio_content_name"), 0, candidate));
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(
                    jcandidate_template->candidate()));
  sdp = kSdpTcpPassiveCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  sdp = kSdpTcpSOCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
}

// This test verifies the deserialization of candidate-attribute
// as per RFC 5245. Candiate-attribute will be of the format
// candidate:<blah>. This format will be used when candidates
// are trickled.
TEST_F(WebRtcSdpTest, DeserializeRawCandidateAttribute) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);

  std::string candidate_attribute = kRawCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(jcandidate_->candidate()));
  EXPECT_EQ(2u, jcandidate.candidate().generation());

  // Candidate line without generation extension.
  candidate_attribute = kRawCandidate;
  Replace(" generation 2", "", &candidate_attribute);
  EXPECT_TRUE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  Candidate expected = jcandidate_->candidate();
  expected.set_generation(0);
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(expected));

  // Candidate line without candidate:
  candidate_attribute = kRawCandidate;
  Replace("candidate:", "", &candidate_attribute);
  EXPECT_FALSE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));

  // Candidate line with IPV6 address.
  EXPECT_TRUE(SdpDeserializeCandidate(kRawIPV6Candidate, &jcandidate));
}

// This test verifies that the deserialization of an invalid candidate string
// fails.
TEST_F(WebRtcSdpTest, DeserializeInvalidCandidiate) {
    JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);

  std::string candidate_attribute = kRawCandidate;
  candidate_attribute.replace(0, 1, "x");
  EXPECT_FALSE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));

  candidate_attribute = kSdpOneCandidate;
  candidate_attribute.replace(0, 1, "x");
  EXPECT_FALSE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));

  candidate_attribute = kRawCandidate;
  candidate_attribute.append("\r\n");
  candidate_attribute.append(kRawCandidate);
  EXPECT_FALSE(SdpDeserializeCandidate(candidate_attribute, &jcandidate));

  EXPECT_FALSE(SdpDeserializeCandidate(kSdpTcpInvalidCandidate, &jcandidate));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithRtpDataChannels) {
  AddRtpDataChannel();
  JsepSessionDescription jdesc(kDummyType);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpRtpDataChannelString);
  JsepSessionDescription jdesc_output(kDummyType);

  // Deserialize
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  // Verify
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannels) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jdesc(kDummyType);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);
  JsepSessionDescription jdesc_output(kDummyType);

  // Verify with DTLS/SCTP (already in kSdpSctpDataChannelString).
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with UDP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kDtlsSctp),
                        strlen(kDtlsSctp), kUdpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with TCP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kUdpDtlsSctp),
                        strlen(kUdpDtlsSctp), kTcpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithSctpPort) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jdesc(kDummyType);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpPort);
  JsepSessionDescription jdesc_output(kDummyType);

  // Verify with DTLS/SCTP (already in kSdpSctpDataChannelStringWithSctpPort).
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with UDP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kDtlsSctp),
                        strlen(kDtlsSctp), kUdpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with TCP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kUdpDtlsSctp),
                        strlen(kUdpDtlsSctp), kTcpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithSctpColonPort) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jdesc(kDummyType);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  JsepSessionDescription jdesc_output(kDummyType);

  // Verify with DTLS/SCTP.
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with UDP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kDtlsSctp),
                        strlen(kDtlsSctp), kUdpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));

  // Verify with TCP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kUdpDtlsSctp),
                        strlen(kUdpDtlsSctp), kTcpDtlsSctp);
  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

// Test to check the behaviour if sctp-port is specified
// on the m= line and in a=sctp-port.
TEST_F(WebRtcSdpTest, DeserializeSdpWithMultiSctpPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jdesc(kDummyType);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  // Append m= attributes
  sdp_with_data.append(kSdpSctpDataChannelString);
  // Append a=sctp-port attribute
  sdp_with_data.append("a=sctp-port 5000\r\n");
  JsepSessionDescription jdesc_output(kDummyType);

  EXPECT_FALSE(SdpDeserialize(sdp_with_data, &jdesc_output));
}

// For crbug/344475.
TEST_F(WebRtcSdpTest, DeserializeSdpWithCorruptedSctpDataChannels) {
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);
  // Remove the "\n" at the end.
  sdp_with_data = sdp_with_data.substr(0, sdp_with_data.size() - 1);
  JsepSessionDescription jdesc_output(kDummyType);

  EXPECT_FALSE(SdpDeserialize(sdp_with_data, &jdesc_output));
  // No crash is a pass.
}

void MutateJsepSctpPort(JsepSessionDescription* jdesc,
                        const SessionDescription& desc) {
  // take our pre-built session description and change the SCTP port.
  cricket::SessionDescription* mutant = desc.Copy();
  DataContentDescription* dcdesc =
      mutant->GetContentDescriptionByName(kDataContentName)->as_data();
  std::vector<cricket::DataCodec> codecs(dcdesc->codecs());
  EXPECT_EQ(1U, codecs.size());
  EXPECT_EQ(cricket::kGoogleSctpDataCodecPlType, codecs[0].id);
  codecs[0].SetParam(cricket::kCodecParamPort, kUnusualSctpPort);
  dcdesc->set_codecs(codecs);

  // note: mutant's owned by jdesc now.
  ASSERT_TRUE(jdesc->Initialize(mutant, kSessionId, kSessionVersion));
  mutant = NULL;
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelAndUnusualPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);

  // First setup the expected JsepSessionDescription.
  JsepSessionDescription jdesc(kDummyType);
  MutateJsepSctpPort(&jdesc, desc_);

  // Then get the deserialized JsepSessionDescription.
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);
  rtc::replace_substrs(kDefaultSctpPortStr, strlen(kDefaultSctpPortStr),
                       kUnusualSctpPortStr, strlen(kUnusualSctpPortStr),
                       &sdp_with_data);
  JsepSessionDescription jdesc_output(kDummyType);

  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithSctpDataChannelAndUnusualPortInAttribute) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);

  JsepSessionDescription jdesc(kDummyType);
  MutateJsepSctpPort(&jdesc, desc_);

  // We need to test the deserialized JsepSessionDescription from
  // kSdpSctpDataChannelStringWithSctpPort for
  // draft-ietf-mmusic-sctp-sdp-07
  // a=sctp-port
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpPort);
  rtc::replace_substrs(kDefaultSctpPortStr, strlen(kDefaultSctpPortStr),
                       kUnusualSctpPortStr, strlen(kUnusualSctpPortStr),
                       &sdp_with_data);
  JsepSessionDescription jdesc_output(kDummyType);

  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithRtpDataChannelsAndBandwidth) {
  // We want to test that deserializing data content limits bandwidth
  // settings (it should never be greater than the default).
  // This should prevent someone from using unlimited data bandwidth through
  // JS and "breaking the Internet".
  // See: https://code.google.com/p/chromium/issues/detail?id=280726
  std::string sdp_with_bandwidth = kSdpString;
  sdp_with_bandwidth.append(kSdpRtpDataChannelString);
  InjectAfter("a=mid:data_content_name\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  JsepSessionDescription jdesc_with_bandwidth(kDummyType);

  EXPECT_FALSE(SdpDeserialize(sdp_with_bandwidth, &jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsAndBandwidth) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  JsepSessionDescription jdesc(kDummyType);
  DataContentDescription* dcd = GetFirstDataContentDescription(&desc_);
  dcd->set_bandwidth(100 * 1000);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_bandwidth = kSdpString;
  sdp_with_bandwidth.append(kSdpSctpDataChannelString);
  InjectAfter("a=mid:data_content_name\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  JsepSessionDescription jdesc_with_bandwidth(kDummyType);

  // SCTP has congestion control, so we shouldn't limit the bandwidth
  // as we do for RTP.
  EXPECT_TRUE(SdpDeserialize(sdp_with_bandwidth, &jdesc_with_bandwidth));
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_with_bandwidth));
}

class WebRtcSdpExtmapTest
  : public WebRtcSdpTest, public testing::WithParamInterface<bool> {
};

TEST_P(WebRtcSdpExtmapTest,
    DeserializeSessionDescriptionWithSessionLevelExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(true, false, encrypted);
}

TEST_P(WebRtcSdpExtmapTest,
    DeserializeSessionDescriptionWithMediaLevelExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(false, true, encrypted);
}

TEST_P(WebRtcSdpExtmapTest,
    DeserializeSessionDescriptionWithInvalidExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(true, true, encrypted);
}

INSTANTIATE_TEST_CASE_P(Encrypted,
                        WebRtcSdpExtmapTest,
                        ::testing::Values(false, true));

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutEndLineBreak) {
  JsepSessionDescription jdesc(kDummyType);
  std::string sdp = kSdpFullString;
  sdp = sdp.substr(0, sdp.size() - 2);  // Remove \r\n at the end.
  // Deserialize
  SdpParseError error;
  EXPECT_FALSE(webrtc::SdpDeserialize(sdp, &jdesc, &error));
  const std::string lastline = "a=ssrc:3 label:video_track_id_1";
  EXPECT_EQ(lastline, error.line);
  EXPECT_EQ("Invalid SDP line.", error.description);
}

TEST_F(WebRtcSdpTest, DeserializeCandidateWithDifferentTransport) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);
  std::string new_sdp = kSdpOneCandidate;
  Replace("udp", "unsupported_transport", &new_sdp);
  EXPECT_FALSE(SdpDeserializeCandidate(new_sdp, &jcandidate));
  new_sdp = kSdpOneCandidate;
  Replace("udp", "uDP", &new_sdp);
  EXPECT_TRUE(SdpDeserializeCandidate(new_sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(jcandidate_->candidate()));
}

TEST_F(WebRtcSdpTest, DeserializeCandidateWithUfragPwd) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);
  EXPECT_TRUE(
      SdpDeserializeCandidate(kSdpOneCandidateWithUfragPwd, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  Candidate ref_candidate = jcandidate_->candidate();
  ref_candidate.set_username("user_rtp");
  ref_candidate.set_password("password_rtp");
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(ref_candidate));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithConferenceFlag) {
  JsepSessionDescription jdesc(kDummyType);

  // Deserialize
  EXPECT_TRUE(SdpDeserialize(kSdpConferenceString, &jdesc));

  // Verify
  cricket::AudioContentDescription* audio =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_AUDIO)
          ->as_audio();
  EXPECT_TRUE(audio->conference_mode());

  cricket::VideoContentDescription* video =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_VIDEO)
          ->as_video();
  EXPECT_TRUE(video->conference_mode());
}

TEST_F(WebRtcSdpTest, SerializeSdpWithConferenceFlag) {
  JsepSessionDescription jdesc(kDummyType);

  // We tested deserialization already above, so just test that if we serialize
  // and deserialize the flag doesn't disappear.
  EXPECT_TRUE(SdpDeserialize(kSdpConferenceString, &jdesc));
  std::string reserialized = webrtc::SdpSerialize(jdesc);
  EXPECT_TRUE(SdpDeserialize(reserialized, &jdesc));

  // Verify.
  cricket::AudioContentDescription* audio =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_AUDIO)
          ->as_audio();
  EXPECT_TRUE(audio->conference_mode());

  cricket::VideoContentDescription* video =
      jdesc.description()
          ->GetContentDescriptionByName(cricket::CN_VIDEO)
          ->as_video();
  EXPECT_TRUE(video->conference_mode());
}

TEST_F(WebRtcSdpTest, DeserializeBrokenSdp) {
  const char kSdpDestroyer[] = "!@#$%^&";
  const char kSdpEmptyType[] = " =candidate";
  const char kSdpEqualAsPlus[] = "a+candidate";
  const char kSdpSpaceAfterEqual[] = "a= candidate";
  const char kSdpUpperType[] = "A=candidate";
  const char kSdpEmptyLine[] = "";
  const char kSdpMissingValue[] = "a=";

  const char kSdpBrokenFingerprint[] = "a=fingerprint:sha-1 "
      "4AAD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  const char kSdpExtraField[] = "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB XXX";
  const char kSdpMissingSpace[] = "a=fingerprint:sha-1"
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  // MD5 is not allowed in fingerprints.
  const char kSdpMd5[] = "a=fingerprint:md5 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B";

  // Broken session description
  ExpectParseFailure("v=", kSdpDestroyer);
  ExpectParseFailure("o=", kSdpDestroyer);
  ExpectParseFailure("s=-", kSdpDestroyer);
  // Broken time description
  ExpectParseFailure("t=", kSdpDestroyer);

  // Broken media description
  ExpectParseFailure("m=audio", "c=IN IP4 74.125.224.39");
  ExpectParseFailure("m=video", kSdpDestroyer);

  // Invalid lines
  ExpectParseFailure("a=candidate", kSdpEmptyType);
  ExpectParseFailure("a=candidate", kSdpEqualAsPlus);
  ExpectParseFailure("a=candidate", kSdpSpaceAfterEqual);
  ExpectParseFailure("a=candidate", kSdpUpperType);

  // Bogus fingerprint replacing a=sendrev. We selected this attribute
  // because it's orthogonal to what we are replacing and hence
  // safe.
  ExpectParseFailure("a=sendrecv", kSdpBrokenFingerprint);
  ExpectParseFailure("a=sendrecv", kSdpExtraField);
  ExpectParseFailure("a=sendrecv", kSdpMissingSpace);
  ExpectParseFailure("a=sendrecv", kSdpMd5);

  // Empty Line
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpEmptyLine);
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpMissingValue);
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithInvalidAttributeValue) {
  // ssrc
  ExpectParseFailure("a=ssrc:1", "a=ssrc:badvalue");
  ExpectParseFailure("a=ssrc-group:FEC 2 3", "a=ssrc-group:FEC badvalue 3");
  // crypto
  ExpectParseFailure("a=crypto:1 ", "a=crypto:badvalue ");
  // rtpmap
  ExpectParseFailure("a=rtpmap:111 ", "a=rtpmap:badvalue ");
  ExpectParseFailure("opus/48000/2", "opus/badvalue/2");
  ExpectParseFailure("opus/48000/2", "opus/48000/badvalue");
  // candidate
  ExpectParseFailure("1 udp 2130706432", "badvalue udp 2130706432");
  ExpectParseFailure("1 udp 2130706432", "1 udp badvalue");
  ExpectParseFailure("192.168.1.5 1234", "192.168.1.5 badvalue");
  ExpectParseFailure("rport 2346", "rport badvalue");
  ExpectParseFailure("rport 2346 generation 2",
                     "rport 2346 generation badvalue");
  // m line
  ExpectParseFailure("m=audio 2345 RTP/SAVPF 111 103 104",
                     "m=audio 2345 RTP/SAVPF 111 badvalue 104");

  // bandwidth
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "b=AS:badvalue\r\n",
                                 "b=AS:badvalue");
  // rtcp-fb
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=rtcp-fb:badvalue nack\r\n",
                                 "a=rtcp-fb:badvalue nack");
  // extmap
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:badvalue http://example.com\r\n",
                                 "a=extmap:badvalue http://example.com");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithReorderedPltypes) {
  JsepSessionDescription jdesc_output(kDummyType);

  const char kSdpWithReorderedPlTypesString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 104 103\r\n"  // Pl type 104 preferred.
      "a=rtpmap:111 opus/48000/2\r\n"  // Pltype 111 listed before 103 and 104
                                       // in the map.
      "a=rtpmap:103 ISAC/16000\r\n"  // Pltype 103 listed before 104 in the map.
      "a=rtpmap:104 ISAC/32000\r\n";

  // Deserialize
  EXPECT_TRUE(SdpDeserialize(kSdpWithReorderedPlTypesString, &jdesc_output));

  const AudioContentDescription* acd =
      GetFirstAudioContentDescription(jdesc_output.description());
  ASSERT_TRUE(acd);
  ASSERT_FALSE(acd->codecs().empty());
  EXPECT_EQ("ISAC", acd->codecs()[0].name);
  EXPECT_EQ(32000, acd->codecs()[0].clockrate);
  EXPECT_EQ(104, acd->codecs()[0].id);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeCodecParams) {
  JsepSessionDescription jdesc_output(kDummyType);
  CodecParams params;
  params.max_ptime = 40;
  params.ptime = 30;
  params.min_ptime = 10;
  params.sprop_stereo = 1;
  params.stereo = 1;
  params.useinband = 1;
  params.maxaveragebitrate = 128000;
  TestDeserializeCodecParams(params, &jdesc_output);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpFb) {
  const bool kUseWildcard = false;
  JsepSessionDescription jdesc_output(kDummyType);
  TestDeserializeRtcpFb(&jdesc_output, kUseWildcard);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpFbWildcard) {
  const bool kUseWildcard = true;
  JsepSessionDescription jdesc_output(kDummyType);
  TestDeserializeRtcpFb(&jdesc_output, kUseWildcard);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtp) {
  JsepSessionDescription jdesc_output(kDummyType);

  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "a=rtpmap:120 VP8/90000\r\n"
      "a=fmtp:120 x-google-min-bitrate=10;x-google-max-quantization=40\r\n";

  // Deserialize
  SdpParseError error;
  EXPECT_TRUE(
      webrtc::SdpDeserialize(kSdpWithFmtpString, &jdesc_output, &error));

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output.description());
  ASSERT_TRUE(vcd);
  ASSERT_FALSE(vcd->codecs().empty());
  cricket::VideoCodec vp8 = vcd->codecs()[0];
  EXPECT_EQ("VP8", vp8.name);
  EXPECT_EQ(120, vp8.id);
  cricket::CodecParameterMap::iterator found =
      vp8.params.find("x-google-min-bitrate");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "10");
  found = vp8.params.find("x-google-max-quantization");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "40");
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtpWithSprops) {
  JsepSessionDescription jdesc_output(kDummyType);

  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 49170 RTP/AVP 98\r\n"
      "a=rtpmap:98 H264/90000\r\n"
      "a=fmtp:98 profile-level-id=42A01E; "
      "sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n";

  // Deserialize.
  SdpParseError error;
  EXPECT_TRUE(
      webrtc::SdpDeserialize(kSdpWithFmtpString, &jdesc_output, &error));

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output.description());
  ASSERT_TRUE(vcd);
  ASSERT_FALSE(vcd->codecs().empty());
  cricket::VideoCodec h264 = vcd->codecs()[0];
  EXPECT_EQ("H264", h264.name);
  EXPECT_EQ(98, h264.id);
  cricket::CodecParameterMap::const_iterator found =
      h264.params.find("profile-level-id");
  ASSERT_TRUE(found != h264.params.end());
  EXPECT_EQ(found->second, "42A01E");
  found = h264.params.find("sprop-parameter-sets");
  ASSERT_TRUE(found != h264.params.end());
  EXPECT_EQ(found->second, "Z0IACpZTBYmI,aMljiA==");
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtpWithSpace) {
  JsepSessionDescription jdesc_output(kDummyType);

  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "a=rtpmap:120 VP8/90000\r\n"
      "a=fmtp:120   x-google-min-bitrate=10;  x-google-max-quantization=40\r\n";

  // Deserialize
  SdpParseError error;
  EXPECT_TRUE(webrtc::SdpDeserialize(kSdpWithFmtpString, &jdesc_output,
                                     &error));

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output.description());
  ASSERT_TRUE(vcd);
  ASSERT_FALSE(vcd->codecs().empty());
  cricket::VideoCodec vp8 = vcd->codecs()[0];
  EXPECT_EQ("VP8", vp8.name);
  EXPECT_EQ(120, vp8.id);
  cricket::CodecParameterMap::iterator found =
      vp8.params.find("x-google-min-bitrate");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "10");
  found = vp8.params.find("x-google-max-quantization");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "40");
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithUnknownParameter) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  cricket::AudioCodecs codecs = acd->codecs();
  codecs[0].params["unknown-future-parameter"] = "SomeFutureValue";
  acd->set_codecs(codecs);

  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:111 opus/48000/2\r\n",
              "a=fmtp:111 unknown-future-parameter=SomeFutureValue\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithKnownFmtpParameter) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  cricket::AudioCodecs codecs = acd->codecs();
  codecs[0].params["stereo"] = "1";
  acd->set_codecs(codecs);

  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:111 opus/48000/2\r\n",
              "a=fmtp:111 stereo=1\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithPTimeAndMaxPTime) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  cricket::AudioCodecs codecs = acd->codecs();
  codecs[0].params["ptime"] = "20";
  codecs[0].params["maxptime"] = "120";
  acd->set_codecs(codecs);

  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:104 ISAC/32000\r\n",
              "a=maxptime:120\r\n"  // No comma here. String merging!
              "a=ptime:20\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeVideoFmtp) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);

  cricket::VideoCodecs codecs = vcd->codecs();
  codecs[0].params["x-google-min-bitrate"] = "10";
  vcd->set_codecs(codecs);

  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:120 VP8/90000\r\n",
              "a=fmtp:120 x-google-min-bitrate=10\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithIceLite) {
  JsepSessionDescription jdesc_with_icelite(kDummyType);
  std::string sdp_with_icelite = kSdpFullString;
  EXPECT_TRUE(SdpDeserialize(sdp_with_icelite, &jdesc_with_icelite));
  cricket::SessionDescription* desc = jdesc_with_icelite.description();
  const cricket::TransportInfo* tinfo1 =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(cricket::ICEMODE_FULL, tinfo1->description.ice_mode);
  const cricket::TransportInfo* tinfo2 =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(cricket::ICEMODE_FULL, tinfo2->description.ice_mode);
  InjectAfter(kSessionTime,
              "a=ice-lite\r\n",
              &sdp_with_icelite);
  EXPECT_TRUE(SdpDeserialize(sdp_with_icelite, &jdesc_with_icelite));
  desc = jdesc_with_icelite.description();
  const cricket::TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(cricket::ICEMODE_LITE, atinfo->description.ice_mode);
  const cricket::TransportInfo* vtinfo =
        desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(cricket::ICEMODE_LITE, vtinfo->description.ice_mode);
}

// Verifies that the candidates in the input SDP are parsed and serialized
// correctly in the output SDP.
TEST_F(WebRtcSdpTest, RoundTripSdpWithSctpDataChannelsWithCandidates) {
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelWithCandidatesString);
  JsepSessionDescription jdesc_output(kDummyType);

  EXPECT_TRUE(SdpDeserialize(sdp_with_data, &jdesc_output));
  EXPECT_EQ(sdp_with_data, webrtc::SdpSerialize(jdesc_output));
}

TEST_F(WebRtcSdpTest, SerializeDtlsSetupAttribute) {
  AddFingerprint();
  TransportInfo audio_transport_info =
      *(desc_.GetTransportInfoByName(kAudioContentName));
  EXPECT_EQ(cricket::CONNECTIONROLE_NONE,
            audio_transport_info.description.connection_role);
  audio_transport_info.description.connection_role =
        cricket::CONNECTIONROLE_ACTIVE;

  TransportInfo video_transport_info =
      *(desc_.GetTransportInfoByName(kVideoContentName));
  EXPECT_EQ(cricket::CONNECTIONROLE_NONE,
            video_transport_info.description.connection_role);
  video_transport_info.description.connection_role =
        cricket::CONNECTIONROLE_ACTIVE;

  desc_.RemoveTransportInfoByName(kAudioContentName);
  desc_.RemoveTransportInfoByName(kVideoContentName);

  desc_.AddTransportInfo(audio_transport_info);
  desc_.AddTransportInfo(video_transport_info);

  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_dtlssetup = kSdpFullString;

  // Fingerprint attribute is necessary to add DTLS setup attribute.
  InjectAfter(kAttributeIcePwdVoice,
              kFingerprint, &sdp_with_dtlssetup);
  InjectAfter(kAttributeIcePwdVideo,
              kFingerprint, &sdp_with_dtlssetup);
  // Now adding |setup| attribute.
  InjectAfter(kFingerprint,
              "a=setup:active\r\n", &sdp_with_dtlssetup);
  EXPECT_EQ(sdp_with_dtlssetup, message);
}

TEST_F(WebRtcSdpTest, DeserializeDtlsSetupAttribute) {
  JsepSessionDescription jdesc_with_dtlssetup(kDummyType);
  std::string sdp_with_dtlssetup = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=setup:actpass\r\n",
              &sdp_with_dtlssetup);
  EXPECT_TRUE(SdpDeserialize(sdp_with_dtlssetup, &jdesc_with_dtlssetup));
  cricket::SessionDescription* desc = jdesc_with_dtlssetup.description();
  const cricket::TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(cricket::CONNECTIONROLE_ACTPASS,
            atinfo->description.connection_role);
  const cricket::TransportInfo* vtinfo =
        desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(cricket::CONNECTIONROLE_ACTPASS,
            vtinfo->description.connection_role);
}

// Verifies that the order of the serialized m-lines follows the order of the
// ContentInfo in SessionDescription, and vise versa for deserialization.
TEST_F(WebRtcSdpTest, MediaContentOrderMaintainedRoundTrip) {
  JsepSessionDescription jdesc(kDummyType);
  const std::string media_content_sdps[3] = {
    kSdpAudioString,
    kSdpVideoString,
    kSdpSctpDataChannelString
  };
  const cricket::MediaType media_types[3] = {
    cricket::MEDIA_TYPE_AUDIO,
    cricket::MEDIA_TYPE_VIDEO,
    cricket::MEDIA_TYPE_DATA
  };

  // Verifies all 6 permutations.
  for (size_t i = 0; i < 6; ++i) {
    size_t media_content_in_sdp[3];
    // The index of the first media content.
    media_content_in_sdp[0] = i / 2;
    // The index of the second media content.
    media_content_in_sdp[1] = (media_content_in_sdp[0] + i % 2 + 1) % 3;
    // The index of the third media content.
    media_content_in_sdp[2] = (media_content_in_sdp[0] + (i + 1) % 2 + 1) % 3;

    std::string sdp_string = kSdpSessionString;
    for (size_t i = 0; i < 3; ++i)
      sdp_string += media_content_sdps[media_content_in_sdp[i]];

    EXPECT_TRUE(SdpDeserialize(sdp_string, &jdesc));
    cricket::SessionDescription* desc = jdesc.description();
    EXPECT_EQ(3u, desc->contents().size());

    for (size_t i = 0; i < 3; ++i) {
      const cricket::MediaContentDescription* mdesc =
          desc->contents()[i].media_description();
      EXPECT_EQ(media_types[media_content_in_sdp[i]], mdesc->type());
    }

    std::string serialized_sdp = webrtc::SdpSerialize(jdesc);
    EXPECT_EQ(sdp_string, serialized_sdp);
  }
}

TEST_F(WebRtcSdpTest, DeserializeBundleOnlyAttribute) {
  MakeBundleOnlyDescription();
  JsepSessionDescription deserialized_description(kDummyType);
  ASSERT_TRUE(
      SdpDeserialize(kBundleOnlySdpFullString, &deserialized_description));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

// The semantics of "a=bundle-only" are only defined when it's used in
// combination with a 0 port on the m= line. We should ignore it if used with a
// nonzero port.
TEST_F(WebRtcSdpTest, IgnoreBundleOnlyWithNonzeroPort) {
  // Make the base bundle-only description but unset the bundle-only flag.
  MakeBundleOnlyDescription();
  jdesc_.description()->contents()[1].bundle_only = false;

  std::string modified_sdp = kBundleOnlySdpFullString;
  Replace("m=video 0", "m=video 9", &modified_sdp);
  JsepSessionDescription deserialized_description(kDummyType);
  ASSERT_TRUE(SdpDeserialize(modified_sdp, &deserialized_description));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

TEST_F(WebRtcSdpTest, SerializeBundleOnlyAttribute) {
  MakeBundleOnlyDescription();
  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, DeserializePlanBSessionDescription) {
  MakePlanBDescription();

  JsepSessionDescription deserialized_description(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kPlanBSdpFullString, &deserialized_description));

  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

TEST_F(WebRtcSdpTest, SerializePlanBSessionDescription) {
  MakePlanBDescription();
  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, DeserializeUnifiedPlanSessionDescription) {
  MakeUnifiedPlanDescription();

  JsepSessionDescription deserialized_description(kDummyType);
  EXPECT_TRUE(
      SdpDeserialize(kUnifiedPlanSdpFullString, &deserialized_description));

  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

TEST_F(WebRtcSdpTest, SerializeUnifiedPlanSessionDescription) {
  MakeUnifiedPlanDescription();
  TestSerialize(jdesc_);
}

// This tests deserializing a Unified Plan SDP that is compatible with both
// Unified Plan and Plan B style SDP. It tests the case for audio/video tracks
// with no stream ids and multiple stream ids. For parsing this, the Unified
// Plan a=msid lines should take priority, because the Plan B style a=ssrc msid
// lines do not support multiple stream ids and no stream ids.
TEST_F(WebRtcSdpTest, DeserializeUnifiedPlanSessionDescriptionSpecialMsid) {
  MakeUnifiedPlanDescriptionMultipleStreamIds();

  JsepSessionDescription deserialized_description(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kUnifiedPlanSdpFullStringWithSpecialMsid,
                             &deserialized_description));

  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

TEST_F(WebRtcSdpTest, SerializeUnifiedPlanSessionDescriptionSpecialMsid) {
  MakeUnifiedPlanDescriptionMultipleStreamIds();
  TestSerialize(jdesc_);
}

// This tests that a Unified Plan SDP with no a=ssrc lines is
// serialized/deserialized appropriately. In this case the
// MediaContentDescription will contain a StreamParams object that doesn't have
// any SSRCs. Vice versa, this will be created upon deserializing an SDP with no
// SSRC lines.
TEST_F(WebRtcSdpTest, DeserializeUnifiedPlanSessionDescriptionNoSsrcSignaling) {
  MakeUnifiedPlanDescription();
  RemoveSsrcSignalingFromStreamParams();
  std::string unified_plan_sdp_string = kUnifiedPlanSdpFullString;
  RemoveSsrcLinesFromSdpString(&unified_plan_sdp_string);

  JsepSessionDescription deserialized_description(kDummyType);
  EXPECT_TRUE(
      SdpDeserialize(unified_plan_sdp_string, &deserialized_description));
  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
}

TEST_F(WebRtcSdpTest, SerializeUnifiedPlanSessionDescriptionNoSsrcSignaling) {
  MakeUnifiedPlanDescription();
  RemoveSsrcSignalingFromStreamParams();

  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, EmptyDescriptionHasNoMsidSignaling) {
  JsepSessionDescription jsep_desc(kDummyType);
  ASSERT_TRUE(SdpDeserialize(kSdpSessionString, &jsep_desc));
  EXPECT_EQ(0, jsep_desc.description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, DataChannelOnlyHasNoMsidSignaling) {
  JsepSessionDescription jsep_desc(kDummyType);
  std::string sdp = kSdpSessionString;
  sdp += kSdpSctpDataChannelString;
  ASSERT_TRUE(SdpDeserialize(sdp, &jsep_desc));
  EXPECT_EQ(0, jsep_desc.description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, PlanBHasSsrcAttributeMsidSignaling) {
  JsepSessionDescription jsep_desc(kDummyType);
  ASSERT_TRUE(SdpDeserialize(kPlanBSdpFullString, &jsep_desc));
  EXPECT_EQ(cricket::kMsidSignalingSsrcAttribute,
            jsep_desc.description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, UnifiedPlanHasMediaSectionMsidSignaling) {
  JsepSessionDescription jsep_desc(kDummyType);
  ASSERT_TRUE(SdpDeserialize(kUnifiedPlanSdpFullString, &jsep_desc));
  EXPECT_EQ(cricket::kMsidSignalingMediaSection,
            jsep_desc.description()->msid_signaling());
}

const char kMediaSectionMsidLine[] = "a=msid:local_stream_1 audio_track_id_1";
const char kSsrcAttributeMsidLine[] =
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1";

TEST_F(WebRtcSdpTest, SerializeOnlyMediaSectionMsid) {
  jdesc_.description()->set_msid_signaling(cricket::kMsidSignalingMediaSection);
  std::string sdp = webrtc::SdpSerialize(jdesc_);

  EXPECT_NE(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_EQ(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

TEST_F(WebRtcSdpTest, SerializeOnlySsrcAttributeMsid) {
  jdesc_.description()->set_msid_signaling(
      cricket::kMsidSignalingSsrcAttribute);
  std::string sdp = webrtc::SdpSerialize(jdesc_);

  EXPECT_EQ(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_NE(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

TEST_F(WebRtcSdpTest, SerializeBothMediaSectionAndSsrcAttributeMsid) {
  jdesc_.description()->set_msid_signaling(
      cricket::kMsidSignalingMediaSection |
      cricket::kMsidSignalingSsrcAttribute);
  std::string sdp = webrtc::SdpSerialize(jdesc_);

  EXPECT_NE(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_NE(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

// Regression test for heap overflow bug:
// https://bugs.chromium.org/p/chromium/issues/detail?id=647916
TEST_F(WebRtcSdpTest, DeserializeSctpPortInVideoDescription) {
  // The issue occurs when the sctp-port attribute is found in a video
  // description. The actual heap overflow occurs when parsing the fmtp line.
  static const char kSdpWithSctpPortInVideoDescription[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 9 UDP/DTLS/SCTP 120\r\n"
      "a=sctp-port 5000\r\n"
      "a=fmtp:108 foo=10\r\n";

  ExpectParseFailure(std::string(kSdpWithSctpPortInVideoDescription),
                     "sctp-port");
}

// Regression test for integer overflow bug:
// https://bugs.chromium.org/p/chromium/issues/detail?id=648071
TEST_F(WebRtcSdpTest, DeserializeLargeBandwidthLimit) {
  // Bandwidth attribute is the max signed 32-bit int, which will get
  // multiplied by 1000 and cause int overflow if not careful.
  static const char kSdpWithLargeBandwidth[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "b=AS:2147483647\r\n"
      "foo=fail\r\n";

  ExpectParseFailure(std::string(kSdpWithLargeBandwidth), "foo=fail");
}

// Similar to the above, except that negative values are illegal, not just
// error-prone as large values are.
// https://bugs.chromium.org/p/chromium/issues/detail?id=675361
TEST_F(WebRtcSdpTest, DeserializingNegativeBandwidthLimitFails) {
  static const char kSdpWithNegativeBandwidth[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "b=AS:-1000\r\n";

  ExpectParseFailure(std::string(kSdpWithNegativeBandwidth), "b=AS:-1000");
}

// An exception to the above rule: a value of -1 for b=AS should just be
// ignored, resulting in "kAutoBandwidth" in the deserialized object.
// Applications historically may be using "b=AS:-1" to mean "no bandwidth
// limit", but this is now what ommitting the attribute entirely will do, so
// ignoring it will have the intended effect.
TEST_F(WebRtcSdpTest, BandwidthLimitOfNegativeOneIgnored) {
  static const char kSdpWithBandwidthOfNegativeOne[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "b=AS:-1\r\n";

  JsepSessionDescription jdesc_output(kDummyType);
  EXPECT_TRUE(SdpDeserialize(kSdpWithBandwidthOfNegativeOne, &jdesc_output));
  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output.description());
  ASSERT_TRUE(vcd);
  EXPECT_EQ(cricket::kAutoBandwidth, vcd->bandwidth());
}

// Test that "ufrag"/"pwd" in the candidate line itself are ignored, and only
// the "a=ice-ufrag"/"a=ice-pwd" attributes are used.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=681286
TEST_F(WebRtcSdpTest, IceCredentialsInCandidateStringIgnored) {
  // Important piece is "ufrag foo pwd bar".
  static const char kSdpWithIceCredentialsInCandidateString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
      "generation 2 ufrag foo pwd bar\r\n";

  JsepSessionDescription jdesc_output(kDummyType);
  EXPECT_TRUE(
      SdpDeserialize(kSdpWithIceCredentialsInCandidateString, &jdesc_output));
  const IceCandidateCollection* candidates = jdesc_output.candidates(0);
  ASSERT_NE(nullptr, candidates);
  ASSERT_EQ(1, candidates->count());
  cricket::Candidate c = candidates->at(0)->candidate();
  EXPECT_EQ("ufrag_voice", c.username());
  EXPECT_EQ("pwd_voice", c.password());
}

// Test that SDP with an invalid port number in "a=candidate" lines is
// rejected, without crashing.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=677029
TEST_F(WebRtcSdpTest, DeserializeInvalidPortInCandidateAttribute) {
  static const char kSdpWithInvalidCandidatePort[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 12345678 typ host "
      "generation 2 raddr 192.168.1.1 rport 87654321\r\n";

  JsepSessionDescription jdesc_output(kDummyType);
  EXPECT_FALSE(SdpDeserialize(kSdpWithInvalidCandidatePort, &jdesc_output));
}

// Test that "a=msid" with a missing track ID is rejected and doesn't crash.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=686405
TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithMissingTrackId) {
  static const char kSdpWithMissingTrackId[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id \r\n";

  JsepSessionDescription jdesc_output(kDummyType);
  EXPECT_FALSE(SdpDeserialize(kSdpWithMissingTrackId, &jdesc_output));
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithMissingStreamId) {
  static const char kSdpWithMissingStreamId[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid: track_id\r\n";

  JsepSessionDescription jdesc_output(kDummyType);
  EXPECT_FALSE(SdpDeserialize(kSdpWithMissingStreamId, &jdesc_output));
}

// Tests that if both session-level address and media-level address exist, use
// the media-level address.
TEST_F(WebRtcSdpTest, ParseConnectionData) {
  JsepSessionDescription jsep_desc(kDummyType);

  // Sesssion-level address.
  std::string sdp = kSdpFullString;
  InjectAfter("s=-\r\n", "c=IN IP4 192.168.0.3\r\n", &sdp);
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));

  const auto& content1 = jsep_desc.description()->contents()[0];
  EXPECT_EQ("74.125.127.126:2345",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc.description()->contents()[1];
  EXPECT_EQ("74.125.224.39:3457",
            content2.media_description()->connection_address().ToString());
}

// Tests that the session-level connection address will be used if the media
// level-addresses are not specified.
TEST_F(WebRtcSdpTest, ParseConnectionDataSessionLevelOnly) {
  JsepSessionDescription jsep_desc(kDummyType);

  // Sesssion-level address.
  std::string sdp = kSdpString;
  InjectAfter("s=-\r\n", "c=IN IP4 192.168.0.3\r\n", &sdp);
  // Remove the media level addresses.
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));

  const auto& content1 = jsep_desc.description()->contents()[0];
  EXPECT_EQ("192.168.0.3:9",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc.description()->contents()[1];
  EXPECT_EQ("192.168.0.3:9",
            content2.media_description()->connection_address().ToString());
}

TEST_F(WebRtcSdpTest, ParseConnectionDataIPv6) {
  JsepSessionDescription jsep_desc(kDummyType);

  std::string sdp = kSdpString;
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));
  Replace("m=audio 9 RTP/SAVPF 111 103 104\r\nc=IN IP4 0.0.0.0\r\n",
          "m=audio 9 RTP/SAVPF 111 103 104\r\nc=IN IP6 "
          "2001:0db8:85a3:0000:0000:8a2e:0370:7335\r\n",
          &sdp);
  Replace("m=video 9 RTP/SAVPF 120\r\nc=IN IP4 0.0.0.0\r\n",
          "m=video 9 RTP/SAVPF 120\r\nc=IN IP6 "
          "2001:0db8:85a3:0000:0000:8a2e:0370:7336\r\n",
          &sdp);
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));
  const auto& content1 = jsep_desc.description()->contents()[0];
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7335]:9",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc.description()->contents()[1];
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7336]:9",
            content2.media_description()->connection_address().ToString());
}

// Test that the invalid or unsupprted connection data cannot be parsed.
TEST_F(WebRtcSdpTest, ParseConnectionDataFailure) {
  JsepSessionDescription jsep_desc(kDummyType);
  std::string sdp = kSdpString;
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));

  // Unsupported multicast IPv4 address.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP4 74.125.224.39/127\r\n", &sdp);
  EXPECT_FALSE(SdpDeserialize(sdp, &jsep_desc));

  // Unsupported multicast IPv6 address.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP6 ::1/3\r\n", &sdp);
  EXPECT_FALSE(SdpDeserialize(sdp, &jsep_desc));

  // Mismatched address type.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP6 74.125.224.39\r\n", &sdp);
  EXPECT_FALSE(SdpDeserialize(sdp, &jsep_desc));

  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n",
          "c=IN IP4 2001:0db8:85a3:0000:0000:8a2e:0370:7334\r\n", &sdp);
  EXPECT_FALSE(SdpDeserialize(sdp, &jsep_desc));
}

TEST_F(WebRtcSdpTest, SerializeAndDeserializeWithConnectionAddress) {
  JsepSessionDescription expected_jsep(kDummyType);
  MakeDescriptionWithoutCandidates(&expected_jsep);
  // Serialization.
  std::string message = webrtc::SdpSerialize(expected_jsep);
  // Deserialization.
  JsepSessionDescription jdesc(kDummyType);
  EXPECT_TRUE(SdpDeserialize(message, &jdesc));
  auto audio_desc = jdesc.description()
                        ->GetContentByName(kAudioContentName)
                        ->media_description();
  auto video_desc = jdesc.description()
                        ->GetContentByName(kVideoContentName)
                        ->media_description();
  EXPECT_EQ(audio_desc_->connection_address().ToString(),
            audio_desc->connection_address().ToString());
  EXPECT_EQ(video_desc_->connection_address().ToString(),
            video_desc->connection_address().ToString());
}

// RFC4566 says "If a session has no meaningful name, the value "s= " SHOULD be
// used (i.e., a single space as the session name)." So we should accept that.
TEST_F(WebRtcSdpTest, DeserializeEmptySessionName) {
  JsepSessionDescription jsep_desc(kDummyType);
  std::string sdp = kSdpString;
  Replace("s=-\r\n", "s= \r\n", &sdp);
  EXPECT_TRUE(SdpDeserialize(sdp, &jsep_desc));
}
