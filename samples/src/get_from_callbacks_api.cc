// Copyright 2018 Slightech Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <iostream>
#include <mutex>

#include <opencv2/highgui/highgui.hpp>

#include "mynteyed/camera.h"
#include "mynteyed/utils.h"

#include "util/cam_utils.h"
#include "util/counter.h"
#include "util/cv_painter.h"

MYNTEYE_USE_NAMESPACE

int main(int argc, char const *argv[]) {
  auto api = API::Create();
  bool ok;
  auto &&request = api->SelectStreamRequest(&ok);

  {
    // Framerate: 10(default), [0,60], [0,30](STREAM_2560x720)
    request.framerate = 30;

    // Device mode, default DEVICE_ALL
    //   DEVICE_COLOR: IMAGE_LEFT_COLOR ✓ IMAGE_RIGHT_COLOR ? IMAGE_DEPTH x
    //   DEVICE_DEPTH: IMAGE_LEFT_COLOR x IMAGE_RIGHT_COLOR x IMAGE_DEPTH ✓
    //   DEVICE_ALL:   IMAGE_LEFT_COLOR ✓ IMAGE_RIGHT_COLOR ? IMAGE_DEPTH ✓
    // Note: ✓: available, x: unavailable, ?: depends on #stream_mode
    // Color mode: raw(default), rectified
    // request.color_mode = ColorMode::COLOR_RECTIFIED;

    // Depth mode: colorful(default), gray, raw
    // request.depth_mode = DepthMode::DEPTH_GRAY;

    // Stream mode: left color only
    // request.stream_mode = StreamMode::STREAM_640x480;  // vga
    // request.stream_mode = StreamMode::STREAM_1280x720;  // hd
    // Stream mode: left+right color
    // request.stream_mode = StreamMode::STREAM_1280x480;  // vga
    request.stream_mode = StreamMode::STREAM_2560x720;
    // Auto-exposure: true(default), false
    // request.state_ae = false;

    // Auto-white balance: true(default), false
    // request.state_awb = false;

    // Infrared intensity: 0(default), [0,10]
  }

  // Enable image infos
  api->EnableImageInfo(false);
  // Enable motion datas
  api->EnableMotionDatas(0);

  // Callbacks
  std::mutex mutex;
  {
    // Set image info callback
    api->SetImgInfoCallback([&mutex](const std::shared_ptr<ImgInfo> &info) {
      std::lock_guard<std::mutex> _(mutex);
      std::cout << "  [img_info] fid: " << info->frame_id
                << ", stamp: " << info->timestamp
                << ", expos: " << info->exposure_time << std::endl
                << std::flush;
    });

    std::vector<Stream> types {
        Stream::IMAGE_LEFT_COLOR,
        Stream::IMAGE_RIGHT_COLOR,
        Stream::IMAGE_DEPTH,
    };
    for (auto &&type : types) {
      // Set stream data callback
      api->SetStreamCallback(type, [&mutex](const StreamData &data) {
        std::lock_guard<std::mutex> _(mutex);
        std::cout << "  [" << data.img->type() << "] fid: "
                  << data.img->frame_id() << std::endl
                  << std::flush;
      });
    }

    // Set motion data callback
    api->SetMotionCallback([&mutex](const MotionData &data) {
      std::lock_guard<std::mutex> _(mutex);
      if (data.imu->flag == MYNTEYE_IMU_ACCEL) {
        std::cout << "[accel] stamp: " << data.imu->timestamp
                  << ", x: " << data.imu->accel[0]
                  << ", y: " << data.imu->accel[1]
                  << ", z: " << data.imu->accel[2]
                  << ", temp: " << data.imu->temperature
                  << std::endl;
      } else if (data.imu->flag == MYNTEYE_IMU_GYRO) {
        std::cout << "[gyro] stamp: " << data.imu->timestamp
                  << ", x: " << data.imu->gyro[0]
                  << ", y: " << data.imu->gyro[1]
                  << ", z: " << data.imu->gyro[2]
                  << ", temp: " << data.imu->temperature
                  << std::endl;
      }
      std::cout << std::flush;
    });
  }

  api->ConfigStreamRequest(request);

  std::cout << std::endl;
  if (!api->IsOpened()) {
    std::cerr << "Error: Open camera failed" << std::endl;
    return 1;
  }
  std::cout << "Open device success" << std::endl
            << std::endl;

  std::cout << "Press ESC/Q on Windows to terminate" << std::endl;

  bool is_left_ok = api->Supports(Stream::IMAGE_LEFT_COLOR);
  bool is_right_ok = api->Supports(Stream::IMAGE_RIGHT_COLOR);
  bool is_depth_ok = api->Supports(Stream::IMAGE_DEPTH);

  if (is_left_ok)
    cv::namedWindow("left color");
  if (is_right_ok)
    cv::namedWindow("right color");
  if (is_depth_ok)
    cv::namedWindow("depth");

  CVPainter painter;
  util::Counter counter;
  for (;;) {
    api->WaitForStreams();
    counter.Update();

    if (is_left_ok) {
      auto left_color = api->GetStreamData(Stream::IMAGE_LEFT_COLOR);
      if (left_color.img) {
        cv::Mat left = left_color.img->To(ImageFormat::COLOR_BGR)->ToMat();
        painter.DrawSize(left, CVPainter::TOP_LEFT);
        painter.DrawStreamData(left, left_color, CVPainter::TOP_RIGHT);
        painter.DrawInformation(left, util::to_string(counter.fps()),
                                CVPainter::BOTTOM_RIGHT);
        cv::imshow("left color", left);
      }
    }

    if (is_right_ok) {
      auto right_color = api->GetStreamData(Stream::IMAGE_RIGHT_COLOR);
      if (right_color.img) {
        cv::Mat right = right_color.img->To(ImageFormat::COLOR_BGR)->ToMat();
        painter.DrawSize(right, CVPainter::TOP_LEFT);
        painter.DrawStreamData(right, right_color, CVPainter::TOP_RIGHT);
        cv::imshow("right color", right);
      }
    }

    if (is_depth_ok) {
      auto image_depth = api->GetStreamData(Stream::IMAGE_DEPTH);
      if (image_depth.img) {
        cv::Mat depth;
        if (request.depth_mode == DepthMode::DEPTH_COLORFUL) {
          depth = image_depth.img->To(ImageFormat::DEPTH_BGR)->ToMat();
        } else {
          depth = image_depth.img->ToMat();
        }
        painter.DrawSize(depth, CVPainter::TOP_LEFT);
        painter.DrawStreamData(depth, image_depth, CVPainter::TOP_RIGHT);
        cv::imshow("depth", depth);
      }
    }

    char key = static_cast<char>(cv::waitKey(1));
    if (key == 27 || key == 'q' || key == 'Q') {  // ESC/Q
      break;
    }
  }

  api->Close();

  return 0;
}