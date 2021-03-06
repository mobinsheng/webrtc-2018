/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <vector>

#include "api/test/create_videocodec_test_fixture.h"
#include "media/base/mediaconstants.h"
#include "modules/video_coding/codecs/test/objc_codec_factory_helper.h"
#include "modules/video_coding/codecs/test/videocodec_test_fixture_impl.h"
#include "test/gtest.h"
#include "test/testsupport/fileutils.h"

namespace webrtc {
namespace test {

namespace {
const int kForemanNumFrames = 300;

TestConfig CreateTestConfig() {
  TestConfig config;
  config.filename = "foreman_cif";
  config.filepath = ResourcePath(config.filename, "yuv");
  config.num_frames = kForemanNumFrames;
  config.hw_encoder = true;
  config.hw_decoder = true;
  return config;
}

std::unique_ptr<VideoCodecTestFixture> CreateTestFixtureWithConfig(
    TestConfig config) {
  auto decoder_factory = CreateObjCDecoderFactory();
  auto encoder_factory = CreateObjCEncoderFactory();
  return CreateVideoCodecTestFixture(
      config, std::move(decoder_factory), std::move(encoder_factory));
}
}  // namespace

// TODO(webrtc:9099): Disabled until the issue is fixed.
// HW codecs don't work on simulators. Only run these tests on device.
// #if TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
// #define MAYBE_TEST TEST
// #else
#define MAYBE_TEST(s, name) TEST(s, DISABLED_##name)
// #endif

// TODO(kthelgason): Use RC Thresholds when the internal bitrateAdjuster is no
// longer in use.
MAYBE_TEST(VideoProcessorIntegrationTestVideoToolbox,
           ForemanCif500kbpsH264CBP) {
  const auto frame_checker = rtc::MakeUnique<
      VideoCodecTestFixtureImpl::H264KeyframeChecker>();
  auto config = CreateTestConfig();
  config.SetCodecSettings(cricket::kH264CodecName, 1, 1, 1, false, false, false,
                          352, 288);
  config.encoded_frame_checker = frame_checker.get();
  auto fixture = CreateTestFixtureWithConfig(config);

  std::vector<RateProfile> rate_profiles = {{500, 30, kForemanNumFrames}};

  std::vector<QualityThresholds> quality_thresholds = {{33, 29, 0.9, 0.82}};

  fixture->RunTest(rate_profiles, nullptr, &quality_thresholds, nullptr,
                   nullptr);
}

MAYBE_TEST(VideoProcessorIntegrationTestVideoToolbox,
           ForemanCif500kbpsH264CHP) {
  const auto frame_checker = rtc::MakeUnique<
      VideoCodecTestFixtureImpl::H264KeyframeChecker>();
  auto config = CreateTestConfig();
  config.h264_codec_settings.profile = H264::kProfileConstrainedHigh;
  config.SetCodecSettings(cricket::kH264CodecName, 1, 1, 1, false, false, false,
                          352, 288);
  config.encoded_frame_checker = frame_checker.get();
  auto fixture = CreateTestFixtureWithConfig(config);

  std::vector<RateProfile> rate_profiles = {{500, 30, kForemanNumFrames}};

  std::vector<QualityThresholds> quality_thresholds = {{33, 30, 0.91, 0.83}};

  fixture->RunTest(rate_profiles, nullptr, &quality_thresholds, nullptr,
                   nullptr);
}

}  // namespace test
}  // namespace webrtc
