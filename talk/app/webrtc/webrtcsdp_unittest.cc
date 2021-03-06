/*
 * libjingle
 * Copyright 2011, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <set>
#include <string>
#include <vector>

#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/webrtcsdp.h"
#include "talk/base/gunit.h"
#include "talk/base/logging.h"
#include "talk/base/messagedigest.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/sslfingerprint.h"
#include "talk/base/stringencode.h"
#include "talk/base/stringutils.h"
#include "talk/p2p/base/constants.h"
#include "talk/session/media/mediasession.h"

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
using cricket::NS_JINGLE_ICE_UDP;
using cricket::NS_JINGLE_RTP;
using cricket::RELAY_PORT_TYPE;
using cricket::SessionDescription;
using cricket::StreamParams;
using cricket::STUN_PORT_TYPE;
using cricket::TransportDescription;
using cricket::TransportInfo;
using cricket::TransportOptions;
using cricket::VideoCodec;
using cricket::VideoContentDescription;
using webrtc::IceCandidateCollection;
using webrtc::IceCandidateInterface;
using webrtc::JsepIceCandidate;
using webrtc::JsepSessionDescription;
using webrtc::SessionDescriptionInterface;

typedef std::vector<Candidate> Candidates;

static const uint32 kCandidatePriority = 2130706432U;  // pref = 1.0
static const char kCandidateUfragVoice[] = "ufrag_voice";
static const char kCandidatePwdVoice[] = "pwd_voice";
static const char kAttributeIcePwdVoice[] = "a=ice-pwd:pwd_voice\r\n";
static const char kCandidateUfragVideo[] = "ufrag_video";
static const char kCandidatePwdVideo[] = "pwd_video";
static const char kCandidateUfragData[] = "ufrag_data";
static const char kCandidatePwdData[] = "pwd_data";
static const char kAttributeIcePwdVideo[] = "a=ice-pwd:pwd_video\r\n";
static const uint32 kCandidateGeneration = 2;
static const char kCandidateFoundation1[] = "a0+B/1";
static const char kCandidateFoundation2[] = "a0+B/2";
static const char kCandidateFoundation3[] = "a0+B/3";
static const char kCandidateFoundation4[] = "a0+B/4";
static const char kFingerprint[] = "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";

static const uint8 kIdentityDigest[] = {0x4A, 0xAD, 0xB9, 0xB1,
                                        0x3F, 0x82, 0x18, 0x3B,
                                        0x54, 0x02, 0x12, 0xDF,
                                        0x3E, 0x5D, 0x49, 0x6B,
                                        0x19, 0xE5, 0x7C, 0xAB};
// Reference sdp string
static const char kSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=audio 2345 RTP/SAVPF 103 104\r\n"
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
    "a=sendrecv\r\n"
    "a=mid:audio_content_name\r\n"
    "a=rtcp-mux\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 CELT/32000/2\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 a0\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:local_stream_1a0\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    "a=ssrc:4 msid:local_stream_2 a0\r\n"
    "a=ssrc:4 mslabel:local_stream_2\r\n"
    "a=ssrc:4 label:local_stream_2a0\r\n"
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
    "a=sendrecv\r\n"
    "a=mid:video_content_name\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 v0\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:local_stream_1v0\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 v1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:local_stream_1v1\r\n"
    "a=ssrc-group:FEC 5 6\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    "a=ssrc:5 msid:local_stream_2 v0\r\n"
    "a=ssrc:5 mslabel:local_stream_2\r\n"
    "a=ssrc:5 label:local_stream_2v0\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n"
    "a=ssrc:6 msid:local_stream_2 v0\r\n"
    "a=ssrc:6 mslabel:local_stream_2\r\n"
    "a=ssrc:6 label:local_stream_2v0\r\n";

// SDP reference string without the candidates.
static const char kSdpString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/SAVPF 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:1 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=sendrecv\r\n"
    "a=mid:audio_content_name\r\n"
    "a=rtcp-mux\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_32 "
    "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32 "
    "dummy_session_params\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 CELT/32000/2\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 a0\r\n"
    "a=ssrc:1 mslabel:local_stream_1\r\n"
    "a=ssrc:1 label:local_stream_1a0\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    "a=ssrc:4 msid:local_stream_2 a0\r\n"
    "a=ssrc:4 mslabel:local_stream_2\r\n"
    "a=ssrc:4 label:local_stream_2a0\r\n"
    "m=video 1 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:1 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=sendrecv\r\n"
    "a=mid:video_content_name\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 v0\r\n"
    "a=ssrc:2 mslabel:local_stream_1\r\n"
    "a=ssrc:2 label:local_stream_1v0\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 v1\r\n"
    "a=ssrc:3 mslabel:local_stream_1\r\n"
    "a=ssrc:3 label:local_stream_1v1\r\n"
    "a=ssrc-group:FEC 5 6\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    "a=ssrc:5 msid:local_stream_2 v0\r\n"
    "a=ssrc:5 mslabel:local_stream_2\r\n"
    "a=ssrc:5 label:local_stream_2v0\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n"
    "a=ssrc:6 msid:local_stream_2 v0\r\n"
    "a=ssrc:6 mslabel:local_stream_2\r\n"
    "a=ssrc:6 label:local_stream_2v0\r\n";

static const char kSdpDataChannelString[] =
    "m=application 1 RTP/SAVPF 101\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:1 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=sendrecv\r\n"
    "a=mid:data_content_name\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:FvLcvU2P3ZWmQxgPAgcDu7Zl9vftYElFOjEzhWs5\r\n"
    "a=rtpmap:101 google-data/90000\r\n"
    "a=ssrc:10 cname:data_channel_cname\r\n"
    "a=ssrc:10 msid:data_channel d0\r\n"
    "a=ssrc:10 mslabel:data_channel\r\n"
    "a=ssrc:10 label:data_channel\r\n";

// One candidate reference string.
static const char kSdpOneCandidate[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n";

// One candidate reference string.
static const char kSdpOneCandidateOldFormat[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host network_name"
    " eth0 username user_rtp password password_rtp generation 2\r\n";

// Session id and version
static const char kSessionId[] = "18446744069414584320";
static const char kSessionVersion[] = "18446462598732840960";

// Ice options
static const char kIceOption1[] = "iceoption1";
static const char kIceOption2[] = "iceoption2";
static const char kIceOption3[] = "iceoption3";

// Content name
static const char kAudioContentName[] = "audio_content_name";
static const char kVideoContentName[] = "video_content_name";
static const char kDataContentName[] = "data_content_name";

// MediaStream 1
static const char kStreamLabel1[] = "local_stream_1";
static const char kStream1Cname[] = "stream_1_cname";
static const char kAudioTrackLabel1[] = "local_stream_1a0";
static const uint32 kAudioTrack1Ssrc = 1;
static const char kVideoTrackLabel1[] = "local_stream_1v0";
static const uint32 kVideoTrack1Ssrc = 2;
static const char kVideoTrackLabel2[] = "local_stream_1v1";
static const uint32 kVideoTrack2Ssrc = 3;

// MediaStream 2
static const char kStreamLabel2[] = "local_stream_2";
static const char kStream2Cname[] = "stream_2_cname";
static const char kAudioTrackLabel2[] = "local_stream_2a0";
static const uint32 kAudioTrack2Ssrc = 4;
static const char kVideoTrackLabel3[] = "local_stream_2v0";
static const uint32 kVideoTrack3Ssrc = 5;
static const uint32 kVideoTrack4Ssrc = 6;

// DataChannel
static const char kDataChannelLabel[] = "data_channel";
static const char kDataChannelCname[] = "data_channel_cname";
static const uint32 kDataChannelSsrc = 10;

// Candidate
static const char kDummyMid[] = "dummy_mid";
static const int kDummyIndex = 123;

// Misc
static const char kDummyString[] = "dummy";

// Helper functions

// Add some extra |newlines| to the |message| after |line|.
static void InjectAfter(const std::string& line,
                        const std::string& newlines,
                        std::string* message) {
  const std::string tmp = line + newlines;
  talk_base::replace_substrs(line.c_str(), line.length(),
                             tmp.c_str(), tmp.length(), message);
}

static void Replace(const std::string& line,
                    const std::string& newlines,
                    std::string* message) {
  talk_base::replace_substrs(line.c_str(), line.length(),
                             newlines.c_str(), newlines.length(), message);
}

static bool ReplaceAndTryToParse(const char* search, const char* replace) {
  JsepSessionDescription desc(kDummyString);
  std::string sdp = kSdpFullString;
  Replace(search, replace, &sdp);
  return webrtc::SdpDeserialize(sdp, &desc);
}

static void ReplaceDirection(cricket::MediaContentDirection direction,
                             std::string* message) {
  std::string new_direction;
  switch (direction) {
    case cricket::MD_INACTIVE:
      new_direction = "a=inactive";
      break;
    case cricket::MD_SENDONLY:
      new_direction = "a=sendonly";
      break;
    case cricket::MD_RECVONLY:
      new_direction = "a=recvonly";
      break;
    case cricket::MD_SENDRECV:
    default:
      new_direction = "a=sendrecv";
      break;
  }
  Replace("a=sendrecv", new_direction, message);
}

static void ReplaceRejected(bool audio_rejected, bool video_rejected,
                            std::string* message) {
  if (audio_rejected) {
    Replace("m=audio 2345", "m=audio 0", message);
  }
  if (video_rejected) {
    Replace("m=video 3457", "m=video 0", message);
  }
}

// WebRtcSdpTest

class WebRtcSdpTest : public testing::Test {
 public:
  WebRtcSdpTest()
     : jdesc_(kDummyString) {
    // AudioContentDescription
    talk_base::scoped_ptr<AudioContentDescription> audio(
        new AudioContentDescription());
    audio_desc_ = audio.get();
    audio->set_rtcp_mux(true);
    StreamParams audio_stream1;
    audio_stream1.name = kAudioTrackLabel1;
    audio_stream1.cname = kStream1Cname;
    audio_stream1.sync_label = kStreamLabel1;
    audio_stream1.ssrcs.push_back(kAudioTrack1Ssrc);
    audio->AddStream(audio_stream1);
    StreamParams audio_stream2;
    audio_stream2.name = kAudioTrackLabel2;
    audio_stream2.cname = kStream2Cname;
    audio_stream2.sync_label = kStreamLabel2;
    audio_stream2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio->AddStream(audio_stream2);
    audio->AddCrypto(CryptoParams(1, "AES_CM_128_HMAC_SHA1_32",
        "inline:NzB4d1BINUAvLEw6UzF3WSJ+PSdFcGdUJShpX1Zj|2^20|1:32",
        "dummy_session_params"));
    audio->AddCodec(AudioCodec(103, "ISAC", 16000, 0, 1, 0));
    audio->AddCodec(AudioCodec(104, "CELT", 32000, 0, 2, 0));
    desc_.AddContent(kAudioContentName, NS_JINGLE_RTP,
                     audio.release());

    // VideoContentDescription
    talk_base::scoped_ptr<VideoContentDescription> video(
        new VideoContentDescription());
    video_desc_ = video.get();
    StreamParams video_stream1;
    video_stream1.name = kVideoTrackLabel1;
    video_stream1.cname = kStream1Cname;
    video_stream1.sync_label = kStreamLabel1;
    video_stream1.ssrcs.push_back(kVideoTrack1Ssrc);
    video->AddStream(video_stream1);
    StreamParams video_stream2;
    video_stream2.name = kVideoTrackLabel2;
    video_stream2.cname = kStream1Cname;
    video_stream2.sync_label = kStreamLabel1;
    video_stream2.ssrcs.push_back(kVideoTrack2Ssrc);
    video->AddStream(video_stream2);
    StreamParams video_stream3;
    video_stream3.name = kVideoTrackLabel3;
    video_stream3.cname = kStream2Cname;
    video_stream3.sync_label = kStreamLabel2;
    video_stream3.ssrcs.push_back(kVideoTrack3Ssrc);
    video_stream3.ssrcs.push_back(kVideoTrack4Ssrc);
    cricket::SsrcGroup ssrc_group(kFecSsrcGroupSemantics, video_stream3.ssrcs);
    video_stream3.ssrc_groups.push_back(ssrc_group);
    video->AddStream(video_stream3);
    video->AddCrypto(CryptoParams(1, "AES_CM_128_HMAC_SHA1_80",
        "inline:d0RmdmcmVCspeEc3QGZiNWpVLFJhQX1cfHAwJSoj|2^20|1:32", ""));
    video->AddCodec(VideoCodec(
        120,
        JsepSessionDescription::kDefaultVideoCodecName,
        JsepSessionDescription::kMaxVideoCodecWidth,
        JsepSessionDescription::kMaxVideoCodecHeight,
        JsepSessionDescription::kDefaultVideoCodecFramerate,
        JsepSessionDescription::kDefaultVideoCodecPreference));

    desc_.AddContent(kVideoContentName, NS_JINGLE_RTP,
                     video.release());

    // TransportInfo
    EXPECT_TRUE(desc_.AddTransportInfo(
        TransportInfo(kAudioContentName,
                      TransportDescription(NS_JINGLE_ICE_UDP,
                                           TransportOptions(),
                                           kCandidateUfragVoice,
                                           kCandidatePwdVoice,
                                           NULL,
                                           Candidates()))));
    EXPECT_TRUE(desc_.AddTransportInfo(
        TransportInfo(kVideoContentName,
                      TransportDescription(NS_JINGLE_ICE_UDP,
                                           TransportOptions(),
                                           kCandidateUfragVideo,
                                           kCandidatePwdVideo,
                                           NULL,
                                           Candidates()))));

    // v4 host
    int port = 1234;
    talk_base::SocketAddress address("192.168.1.5", port++);
    Candidate candidate1(
        "", ICE_CANDIDATE_COMPONENT_RTP, "udp", address, kCandidatePriority,
        "", "", LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate2(
        "", ICE_CANDIDATE_COMPONENT_RTCP, "udp", address, kCandidatePriority,
        "", "", LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate3(
        "", ICE_CANDIDATE_COMPONENT_RTCP, "udp", address, kCandidatePriority,
        "", "", LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate4(
        "", ICE_CANDIDATE_COMPONENT_RTP, "udp", address, kCandidatePriority,
        "", "", LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation1);

    // v6 host
    talk_base::SocketAddress v6_address("::1", port++);
    cricket::Candidate candidate5(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTP,
        "udp", v6_address, kCandidatePriority,
        "", "", cricket::LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate6(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTCP,
        "udp", v6_address, kCandidatePriority,
        "", "", cricket::LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate7(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTCP,
        "udp", v6_address, kCandidatePriority,
        "", "", cricket::LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    cricket::Candidate candidate8(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTP,
        "udp", v6_address, kCandidatePriority,
        "", "", cricket::LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation2);

    // stun
    int port_stun = 2345;
    talk_base::SocketAddress address_stun("74.125.127.126", port_stun++);
    talk_base::SocketAddress rel_address_stun("192.168.1.5", port_stun++);
    cricket::Candidate candidate9
        ("", cricket::ICE_CANDIDATE_COMPONENT_RTP,
         "udp", address_stun, kCandidatePriority,
         "", "", STUN_PORT_TYPE,
         "", kCandidateGeneration, kCandidateFoundation3);
    candidate9.set_related_address(rel_address_stun);

    address_stun.SetPort(port_stun++);
    rel_address_stun.SetPort(port_stun++);
    cricket::Candidate candidate10(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTCP,
        "udp", address_stun, kCandidatePriority,
        "", "", STUN_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation3);
    candidate10.set_related_address(rel_address_stun);

    // relay
    int port_relay = 3456;
    talk_base::SocketAddress address_relay("74.125.224.39", port_relay++);
    cricket::Candidate candidate11(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTCP,
        "udp", address_relay, kCandidatePriority,
        "", "",
        cricket::RELAY_PORT_TYPE, "",
        kCandidateGeneration, kCandidateFoundation4);
    address_relay.SetPort(port_relay++);
    cricket::Candidate candidate12(
        "", cricket::ICE_CANDIDATE_COMPONENT_RTP,
        "udp", address_relay, kCandidatePriority,
        "", "",
        RELAY_PORT_TYPE, "",
        kCandidateGeneration, kCandidateFoundation4);

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

  bool CompareSessionDescription(const SessionDescription& desc1,
                                 const SessionDescription& desc2) {
    const ContentInfo* ac1 = GetFirstAudioContent(&desc1);
    const AudioContentDescription* acd1 =
        static_cast<const AudioContentDescription*>(ac1->description);
    const ContentInfo* vc1 = GetFirstVideoContent(&desc1);
    const VideoContentDescription* vcd1 =
        static_cast<const VideoContentDescription*>(vc1->description);

    const ContentInfo* ac2 = GetFirstAudioContent(&desc2);
    const AudioContentDescription* acd2 =
        static_cast<const AudioContentDescription*>(ac2->description);
    const ContentInfo* vc2 = GetFirstVideoContent(&desc2);
    const VideoContentDescription* vcd2 =
        static_cast<const VideoContentDescription*>(vc2->description);

    // content name
    EXPECT_EQ(ac1->name, ac2->name);
    EXPECT_EQ(vc1->name, vc2->name);

    // type
    EXPECT_EQ(ac1->type, ac2->type);
    EXPECT_EQ(vc1->type, vc2->type);

    // content direction
    EXPECT_EQ(acd1->direction(), acd2->direction());
    EXPECT_EQ(vcd1->direction(), vcd2->direction());

    // rejected
    EXPECT_EQ(ac1->rejected, ac2->rejected);
    EXPECT_EQ(vc1->rejected, vc2->rejected);

    // rtcp_mux
    EXPECT_EQ(acd1->rtcp_mux(), acd2->rtcp_mux());
    EXPECT_EQ(vcd1->rtcp_mux(), vcd2->rtcp_mux());

    // cryptos
    EXPECT_EQ(acd1->cryptos().size(), acd2->cryptos().size());
    EXPECT_EQ(vcd1->cryptos().size(), vcd2->cryptos().size());
    if (acd1->cryptos().size() != acd2->cryptos().size() ||
        vcd1->cryptos().size() != vcd2->cryptos().size()) {
      return false;
    }
    for (size_t i = 0; i< acd1->cryptos().size(); ++i) {
      const CryptoParams c1 = acd1->cryptos().at(i);
      const CryptoParams c2 = acd2->cryptos().at(i);
      EXPECT_TRUE(c1.Matches(c2));
    }
    for (size_t i = 0; i< vcd1->cryptos().size(); ++i) {
      const CryptoParams c1 = vcd1->cryptos().at(i);
      const CryptoParams c2 = vcd2->cryptos().at(i);
      EXPECT_TRUE(c1.Matches(c2));
    }

    // codecs
    EXPECT_EQ(acd1->codecs().size(), acd2->codecs().size());
    if (acd1->codecs().size() != acd2->codecs().size())
      return false;
    EXPECT_EQ(vcd1->codecs().size(), vcd2->codecs().size());
    if (vcd1->codecs().size() != vcd2->codecs().size())
      return false;
    for (size_t i = 0; i< acd1->codecs().size(); ++i) {
      const AudioCodec c1 = acd1->codecs().at(i);
      const AudioCodec c2 = acd2->codecs().at(i);
      EXPECT_TRUE(c2.Matches(c1));
    }
    for (size_t i = 0; i< vcd1->codecs().size(); ++i) {
      const VideoCodec c1 = vcd1->codecs().at(i);
      const VideoCodec c2 = vcd2->codecs().at(i);
      EXPECT_TRUE(c1.Matches(c2));
      EXPECT_EQ(c1.id, c2.id);
      EXPECT_EQ(c1.width, c2.width);
      EXPECT_EQ(c1.height, c2.height);
      EXPECT_EQ(c1.framerate, c2.framerate);
    }

    // bandwidth
    EXPECT_EQ(acd1->bandwidth(), acd2->bandwidth());
    EXPECT_EQ(vcd1->bandwidth(), vcd2->bandwidth());

    // streams
    EXPECT_EQ(acd1->streams(), acd2->streams());
    EXPECT_EQ(vcd1->streams(), vcd2->streams());

    // group
    const cricket::ContentGroups groups1 = desc1.groups();
    const cricket::ContentGroups groups2 = desc2.groups();
    EXPECT_EQ(groups1.size(), groups1.size());
    if (groups1.size() != groups2.size()) {
      return false;
    }
    for (size_t i = 0; i < groups1.size(); ++i) {
      const cricket::ContentGroup group1 = groups1.at(i);
      const cricket::ContentGroup group2 = groups2.at(i);
      EXPECT_EQ(group1.semantics(), group2.semantics());
      const cricket::ContentNames names1 = group1.content_names();
      const cricket::ContentNames names2 = group2.content_names();
      EXPECT_EQ(names1.size(), names2.size());
      if (names1.size() != names2.size()) {
        return false;
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
      return false;
    }
    for (size_t i = 0; i < transports1.size(); ++i) {
      const cricket::TransportInfo transport1 = transports1.at(i);
      const cricket::TransportInfo transport2 = transports2.at(i);
      EXPECT_EQ(transport1.content_name, transport2.content_name);
      EXPECT_EQ(transport1.description.transport_type,
                transport2.description.transport_type);
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
      EXPECT_TRUE(CompareCandidates(transport1.description.candidates,
                                    transport2.description.candidates));
    }
    return true;
  }

  bool CompareCandidates(const Candidates& cs1, const Candidates& cs2) {
    EXPECT_EQ(cs1.size(), cs2.size());
    if (cs1.size() != cs2.size())
      return false;
    for (size_t i = 0; i< cs1.size(); ++i) {
      const Candidate c1 = cs1.at(i);
      const Candidate c2 = cs2.at(i);
      EXPECT_TRUE(c1.IsEquivalent(c2));
    }
    return true;
  }

  bool CompareJsepSessionDescription(
      const JsepSessionDescription& desc1,
      const JsepSessionDescription& desc2) {
    EXPECT_EQ(desc1.session_id(), desc2.session_id());
    EXPECT_EQ(desc1.session_version(), desc2.session_version());
    EXPECT_TRUE(CompareSessionDescription(*desc1.description(),
                                          *desc2.description()));
    if (desc1.number_of_mediasections() != desc2.number_of_mediasections())
      return false;
    for (size_t i = 0; i < desc1.number_of_mediasections(); ++i) {
      const IceCandidateCollection* cc1 = desc1.candidates(i);
      const IceCandidateCollection* cc2 = desc2.candidates(i);
      if (cc1->count() != cc2->count())
        return false;
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
    talk_base::replace_substrs(ice_ufrag, strlen(ice_ufrag),
        ice_ufragx, strlen(ice_ufragx), sdp);
    talk_base::replace_substrs(ice_pwd, strlen(ice_pwd),
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
      ASSERT(false);
    }
    TransportInfo transport_info(content_name,
                                 TransportDescription(NS_JINGLE_ICE_UDP,
                                                      TransportOptions(),
                                                      ufrag, pwd,
                                                      NULL,
                                                      Candidates()));
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
                     const TransportOptions& transport_options) {
    ASSERT_TRUE(desc_.GetTransportInfoByName(content_name) != NULL);
    cricket::TransportInfo transport_info =
        *(desc_.GetTransportInfoByName(content_name));
    desc_.RemoveTransportInfoByName(content_name);
    transport_info.description.transport_options = transport_options;
    desc_.AddTransportInfo(transport_info);
  }

  void AddFingerprint() {
    desc_.RemoveTransportInfoByName(kAudioContentName);
    desc_.RemoveTransportInfoByName(kVideoContentName);
    talk_base::SSLFingerprint fingerprint(talk_base::DIGEST_SHA_1,
                                          kIdentityDigest,
                                          sizeof(kIdentityDigest));
    EXPECT_TRUE(desc_.AddTransportInfo(
        TransportInfo(kAudioContentName,
                      TransportDescription(NS_JINGLE_ICE_UDP,
                                           TransportOptions(),
                                           kCandidateUfragVoice,
                                           kCandidatePwdVoice,
                                           &fingerprint,
                                           Candidates()))));
    EXPECT_TRUE(desc_.AddTransportInfo(
        TransportInfo(kVideoContentName,
                      TransportDescription(NS_JINGLE_ICE_UDP,
                                           TransportOptions(),
                                           kCandidateUfragVideo,
                                           kCandidatePwdVideo,
                                           &fingerprint,
                                           Candidates()))));
  }

  bool TestSerializeDirection(cricket::MediaContentDirection direction) {
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
    audio_desc_ = static_cast<AudioContentDescription*>(
        audio_desc_->Copy());
    video_desc_ = static_cast<VideoContentDescription*>(
        video_desc_->Copy());
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, NS_JINGLE_RTP, audio_rejected,
                     audio_desc_);
    desc_.AddContent(kVideoContentName, NS_JINGLE_RTP, video_rejected,
                     video_desc_);
    std::string new_sdp = kSdpFullString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);

    if (!jdesc_.Initialize(desc_.Copy(),
                           jdesc_.session_id(),
                           jdesc_.session_version())) {
      return false;
    }
    std::string message = webrtc::SdpSerialize(jdesc_);
    EXPECT_EQ(new_sdp, message);
    return true;
  }

  void AddDataChannel() {
    talk_base::scoped_ptr<DataContentDescription> data(
        new DataContentDescription());
    data_desc_ = data.get();

    data_desc_->AddCodec(DataCodec(101, "google-data", 0));
    StreamParams data_stream;
    data_stream.name = kDataChannelLabel;
    data_stream.cname = kDataChannelCname;
    data_stream.sync_label = kDataChannelLabel;
    data_stream.ssrcs.push_back(kDataChannelSsrc);
    data_desc_->AddStream(data_stream);
    data_desc_->AddCrypto(CryptoParams(
        1, "AES_CM_128_HMAC_SHA1_80",
        "inline:FvLcvU2P3ZWmQxgPAgcDu7Zl9vftYElFOjEzhWs5", ""));
    desc_.AddContent(kDataContentName, NS_JINGLE_RTP, data.release());
    EXPECT_TRUE(desc_.AddTransportInfo(
           TransportInfo(kDataContentName,
                         TransportDescription(NS_JINGLE_ICE_UDP,
                                              TransportOptions(),
                                              kCandidateUfragData,
                                              kCandidatePwdData,
                                              NULL,
                                              Candidates()))));
  }

  bool TestDeserializeDirection(cricket::MediaContentDirection direction) {
    std::string new_sdp = kSdpFullString;
    ReplaceDirection(direction, &new_sdp);
    JsepSessionDescription new_jdesc(kDummyString);

    EXPECT_TRUE(webrtc::SdpDeserialize(new_sdp,
                                       &new_jdesc));

    audio_desc_->set_direction(direction);
    video_desc_->set_direction(direction);
    if (!jdesc_.Initialize(desc_.Copy(),
                           jdesc_.session_id(),
                           jdesc_.session_version())) {
      return false;
    }
    EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, new_jdesc));
    return true;
  }

  bool TestDeserializeRejected(bool audio_rejected, bool video_rejected) {
    std::string new_sdp = kSdpFullString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);
    JsepSessionDescription new_jdesc(JsepSessionDescription::kOffer);

    EXPECT_TRUE(webrtc::SdpDeserialize(new_sdp,
                                       &new_jdesc));
    audio_desc_ = static_cast<AudioContentDescription*>(
        audio_desc_->Copy());
    video_desc_ = static_cast<VideoContentDescription*>(
        video_desc_->Copy());
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, NS_JINGLE_RTP, audio_rejected,
                     audio_desc_);
    desc_.AddContent(kVideoContentName, NS_JINGLE_RTP, video_rejected,
                     video_desc_);
    if (!jdesc_.Initialize(desc_.Copy(),
                           jdesc_.session_id(),
                           jdesc_.session_version())) {
      return false;
    }
    EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, new_jdesc));
    return true;
  }

 protected:
  SessionDescription desc_;
  AudioContentDescription* audio_desc_;
  VideoContentDescription* video_desc_;
  DataContentDescription* data_desc_;
  Candidates candidates_;
  talk_base::scoped_ptr<IceCandidateInterface> jcandidate_;
  JsepSessionDescription jdesc_;
};

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescription) {
  // JsepSessionDescription with desc and candidates.
  std::string message = webrtc::SdpSerialize(jdesc_);
  EXPECT_EQ(std::string(kSdpFullString), message);
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionEmpty) {
  JsepSessionDescription jdesc_empty(kDummyString);
  EXPECT_EQ("", webrtc::SdpSerialize(jdesc_empty));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithFingerprint) {
  AddFingerprint();
  JsepSessionDescription jdesc_with_fingerprint("dummy");
  ASSERT_TRUE(jdesc_with_fingerprint.Initialize(desc_.Copy(),
                                                kSessionId, kSessionVersion));
  std::string message = webrtc::SdpSerialize(jdesc_with_fingerprint);

  std::string sdp_with_fingerprint = kSdpString;
  InjectAfter(kAttributeIcePwdVoice,
              kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo,
              kFingerprint, &sdp_with_fingerprint);

  EXPECT_EQ(sdp_with_fingerprint, message);
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithoutCandidates) {
  // JsepSessionDescription with desc but without candidates.
  JsepSessionDescription jdesc_no_candidates(kDummyString);
  ASSERT_TRUE(jdesc_no_candidates.Initialize(desc_.Copy(),
                                             kSessionId, kSessionVersion));
  std::string message = webrtc::SdpSerialize(jdesc_no_candidates);
  EXPECT_EQ(std::string(kSdpString), message);
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithBundle) {
  ContentGroup group(cricket::GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioContentName);
  group.AddContentName(kVideoContentName);
  desc_.AddGroup(group);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter("t=0 0\r\n",
              "a=group:BUNDLE audio_content_name video_content_name\r\n",
              &sdp_with_bundle);
  EXPECT_EQ(sdp_with_bundle, message);
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithBandwidth) {
  const ContentInfo* content_info = GetFirstVideoContent(&desc_);
  VideoContentDescription* video_content_description =
      static_cast<VideoContentDescription*>(content_info->description);
  video_content_description->set_bandwidth(100 * 1000);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  std::string message = webrtc::SdpSerialize(jdesc_);
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  EXPECT_EQ(sdp_with_bandwidth, message);
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithIceOptions) {
  TransportOptions transport_options;
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

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(cricket::MD_RECVONLY));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(cricket::MD_SENDONLY));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestSerializeDirection(cricket::MD_INACTIVE));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithAudioRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithAudioVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, true));
}

TEST_F(WebRtcSdpTest, SerializeJsepSessionDescriptionWithDataChannel) {
  AddDataChannel();
  JsepSessionDescription jsep_desc(kDummyString);

  ASSERT_TRUE(jsep_desc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));
  std::string message = webrtc::SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpDataChannelString);
  EXPECT_EQ(message, expected_sdp);
}

TEST_F(WebRtcSdpTest, SerializeCandidates) {
  std::string message = webrtc::SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kSdpOneCandidate), message);
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescription) {
  JsepSessionDescription jdesc(kDummyString);
  // Deserialize
  EXPECT_TRUE(webrtc::SdpDeserialize(kSdpFullString, &jdesc));
  // Verify
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithoutCandidates) {
  // JsepSessionDescription with desc but without candidates.
  JsepSessionDescription jdesc_no_candidates(kDummyString);
  ASSERT_TRUE(jdesc_no_candidates.Initialize(desc_.Copy(),
                                             kSessionId, kSessionVersion));
  JsepSessionDescription new_jdesc(kDummyString);
  EXPECT_TRUE(webrtc::SdpDeserialize(kSdpString, &new_jdesc));
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_no_candidates, new_jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithFingerprint) {
  // JsepSessionDescription with DTLS fingerprint.
  AddFingerprint();
  JsepSessionDescription new_jdesc("dummy");
  ASSERT_TRUE(new_jdesc.Initialize(desc_.Copy(),
                                   jdesc_.session_id(),
                                   jdesc_.session_version()));

  JsepSessionDescription jdesc_with_fingerprint("dummy");
  std::string sdp_with_fingerprint = kSdpString;
  InjectAfter(kAttributeIcePwdVoice, kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo,
              kFingerprint,
              &sdp_with_fingerprint);
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_with_fingerprint,
                                     &jdesc_with_fingerprint));
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_with_fingerprint, new_jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithBundle) {
  JsepSessionDescription jdesc_with_bundle(kDummyString);
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter("t=0 0\r\n",
              "a=group:BUNDLE audio_content_name video_content_name\r\n",
              &sdp_with_bundle);
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_with_bundle, &jdesc_with_bundle));
  ContentGroup group(cricket::GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioContentName);
  group.AddContentName(kVideoContentName);
  desc_.AddGroup(group);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc_with_bundle));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithBandwidth) {
  JsepSessionDescription jdesc_with_bandwidth(kDummyString);
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\n",
              "b=AS:100\r\n",
              &sdp_with_bandwidth);
  EXPECT_TRUE(
      webrtc::SdpDeserialize(sdp_with_bandwidth, &jdesc_with_bandwidth));
  const ContentInfo* content_info = GetFirstVideoContent(&desc_);
  VideoContentDescription* video_content_description =
      static_cast<VideoContentDescription*>(content_info->description);
  video_content_description->set_bandwidth(100 * 1000);
  ASSERT_TRUE(jdesc_.Initialize(desc_.Copy(),
                                jdesc_.session_id(),
                                jdesc_.session_version()));
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithIceOptions) {
  JsepSessionDescription jdesc_with_ice_options(kDummyString);
  std::string sdp_with_ice_options = kSdpFullString;
  InjectAfter("t=0 0\r\n",
              "a=ice-options:iceoption3\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVoice,
              "a=ice-options:iceoption1\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVideo,
              "a=ice-options:iceoption2\r\n",
              &sdp_with_ice_options);
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_with_ice_options,
                                     &jdesc_with_ice_options));
  TransportOptions transport_options;
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
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc_with_ice_options));
}

TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithUfragPwd) {
  // Remove the original ice-ufrag and ice-pwd
  JsepSessionDescription jdesc_with_ufrag_pwd(kDummyString);
  std::string sdp_with_ufrag_pwd = kSdpFullString;
  EXPECT_TRUE(RemoveCandidateUfragPwd(&sdp_with_ufrag_pwd));
  // Add session level ufrag and pwd
  InjectAfter("t=0 0\r\n",
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
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_with_ufrag_pwd,
                                     &jdesc_with_ufrag_pwd));
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc_with_ufrag_pwd));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(cricket::MD_RECVONLY));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(cricket::MD_SENDONLY));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestDeserializeDirection(cricket::MD_INACTIVE));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithRejectedAudio) {
  EXPECT_TRUE(TestDeserializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithRejectedVideo) {
  EXPECT_TRUE(TestDeserializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, DeSerializeJsepSessionDescriptionWithRejectedAudioVideo) {
  EXPECT_TRUE(TestDeserializeRejected(true, true));
}

// Tests that we can still handle the sdp uses mslabel and label instead of
// msid for backward compatibility.
TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithoutMsid) {
  JsepSessionDescription jdesc(kDummyString);
  std::string sdp_without_msid = kSdpFullString;
  Replace("msid", "xmsid", &sdp_without_msid);
  // Deserialize
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_without_msid, &jdesc));
  // Verify
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeCandidate) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);

  std::string sdp = kSdpOneCandidate;
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(jcandidate_->candidate()));

  // Candidate line without generation extension.
  sdp = kSdpOneCandidate;
  Replace(" generation 2", "", &sdp);
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  Candidate expected = jcandidate_->candidate();
  expected.set_generation(0);
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(expected));

  // Multiple candidate lines.
  // Only the first line will be deserialized. The rest will be ignored.
  sdp = kSdpOneCandidate;
  sdp.append("a=candidate:1 2 tcp 1234 192.168.1.100 5678 typ host\r\n");
  EXPECT_TRUE(SdpDeserializeCandidate(sdp, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(jcandidate_->candidate()));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithDataChannels) {
  AddDataChannel();
  JsepSessionDescription jdesc(kDummyString);
  ASSERT_TRUE(jdesc.Initialize(desc_.Copy(), kSessionId, kSessionVersion));

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpDataChannelString);
  JsepSessionDescription jdesc_output(kDummyString);

  // Deserialize
  EXPECT_TRUE(webrtc::SdpDeserialize(sdp_with_data, &jdesc_output));
  // Verify
  EXPECT_TRUE(CompareJsepSessionDescription(jdesc, jdesc_output));
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

TEST_F(WebRtcSdpTest, DeserializeCandidateOldFormat) {
  JsepIceCandidate jcandidate(kDummyMid, kDummyIndex);
  EXPECT_TRUE(SdpDeserializeCandidate(kSdpOneCandidateOldFormat, &jcandidate));
  EXPECT_EQ(kDummyMid, jcandidate.sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate.sdp_mline_index());
  Candidate ref_candidate = jcandidate_->candidate();
  ref_candidate.set_username("user_rtp");
  ref_candidate.set_password("password_rtp");
  EXPECT_TRUE(jcandidate.candidate().IsEquivalent(ref_candidate));
}

TEST_F(WebRtcSdpTest, DeserializeBrokenSdp) {
  const char kSdpDestroyer[] = "!@#$%^&";
  const char kSdpInvalidLine1[] = " =candidate";
  const char kSdpInvalidLine2[] = "a+candidate";
  const char kSdpInvalidLine3[] = "a= candidate";
  // Broken fingerprint.
  const char kSdpInvalidLine4[] = "a=fingerprint:sha-1 "
      "4AAD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";
  // Extra field.
  const char kSdpInvalidLine5[] = "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB XXX\r\n";
  // Missing space.
  const char kSdpInvalidLine6[] = "a=fingerprint:sha-1"
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";

  // Broken session description
  EXPECT_FALSE(ReplaceAndTryToParse("v=", kSdpDestroyer));
  EXPECT_FALSE(ReplaceAndTryToParse("o=", kSdpDestroyer));
  EXPECT_FALSE(ReplaceAndTryToParse("s=-", kSdpDestroyer));
  // Broken time description
  EXPECT_FALSE(ReplaceAndTryToParse("t=", kSdpDestroyer));

  // Broken media description
  EXPECT_FALSE(ReplaceAndTryToParse("m=video", kSdpDestroyer));

  // Invalid lines
  EXPECT_FALSE(ReplaceAndTryToParse("a=candidate", kSdpInvalidLine1));
  EXPECT_FALSE(ReplaceAndTryToParse("a=candidate", kSdpInvalidLine2));
  EXPECT_FALSE(ReplaceAndTryToParse("a=candidate", kSdpInvalidLine3));

  // Bogus fingerprint replacing a=sendrev. We selected this attribute
  // because it's orthogonal to what we are replacing and hence
  // safe.
  EXPECT_FALSE(ReplaceAndTryToParse("a=sendrecv", kSdpInvalidLine4));
  EXPECT_FALSE(ReplaceAndTryToParse("a=sendrecv", kSdpInvalidLine5));
  EXPECT_FALSE(ReplaceAndTryToParse("a=sendrecv", kSdpInvalidLine6));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithReorderedPltypes) {
  JsepSessionDescription jdesc_output(kDummyString);

  const char kSdpWithReorderedPlTypesString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 1 RTP/SAVPF 104 103\r\n"  // Pl type 104 preferred.
      "a=rtpmap:103 ISAC/16000\r\n"  // Pltype 103 listed before 104 in the map.
      "a=rtpmap:104 CELT/32000/2\r\n";

  // Deserialize
  EXPECT_TRUE(webrtc::SdpDeserialize(kSdpWithReorderedPlTypesString,
                                     &jdesc_output));

  const ContentInfo* ac = GetFirstAudioContent(jdesc_output.description());
  ASSERT_TRUE(ac != NULL);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  ASSERT_FALSE(acd->codecs().empty());
  EXPECT_EQ("CELT", acd->codecs()[0].name);
  EXPECT_EQ(104, acd->codecs()[0].id);
}
