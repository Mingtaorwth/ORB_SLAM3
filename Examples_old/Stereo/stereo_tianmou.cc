#include "dual_camera.hpp"
#include <ctime>
#include <thread>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <System.h>
#include <opencv2/core/core.hpp>


double angular_velocity_x, angular_velocity_y, angular_velocity_z;
double linear_acceleration_x, linear_acceleration_y, linear_acceleration_z;

// 用于存储IMU数据
struct IMUData {
    double timestamp;
    double ax, ay, az;
    double gx, gy, gz;
};

std::vector<IMUData> imu_data_buffer;

void ImuCallback(const sensor_msgs::Imu::ConstPtr& msg) {
    // 通过回调函数获取IMU数据并存储
    IMUData data;
    data.timestamp = msg->header.stamp.toSec();
    data.ax = msg->linear_acceleration.x;
    data.ay = msg->linear_acceleration.y;
    data.az = msg->linear_acceleration.z;
    data.gx = msg->angular_velocity.x;
    data.gy = msg->angular_velocity.y;
    data.gz = msg->angular_velocity.z;
    
    imu_data_buffer.push_back(data);
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "stereo_tianmou_node");
    ros::NodeHandle nh;

    if(argc < 3 || argc > 4) {
        ROS_ERROR("Usage: ./stereo_tianmou path_to_vocabulary path_to_settings");
        return 1;
    }

    std::string file_name;
    bool bFileName = false;

    if (argc == 4) {
        file_name = std::string(argv[argc - 1]);
        bFileName = true;
    }

    vector<ORB_SLAM3::IMU::Point> vImuMeas;

    ORB_SLAM3::System SLAM(argv[1], argv[2], ORB_SLAM3::System::STEREO, true, 0, file_name);
    float imageScale = SLAM.GetImageScale();

    DualCamera camera;
    camera.DataListener();

    cv::FileStorage fscv("/home/mingtao/THU/calibration/warping.xml", cv::FileStorage::READ);
    cv::Mat map1_left, map2_left, map1_right, map2_right;

    fscv["map1_left"] >> map1_left;
    fscv["map2_left"] >> map2_left;
    fscv["map1_right"] >> map1_right;
    fscv["map2_right"] >> map2_right;
    fscv.release();

    // 订阅 IMU 数据话题
    ros::Subscriber sub = nh.subscribe("/imu", 10, ImuCallback);

    // 设置循环频率
    ros::Rate loop_rate(400);  // 150 Hz

    double previous_tframe = ros::Time::now().toSec();  // 记录上一帧的时间戳
    IMUData previous_imu_data; // 用于存储上一帧的 IMU 数据

    while (ros::ok()) {
        // 获取当前时间戳
        ros::Time current_time = ros::Time::now();
        double tframe = current_time.toSec();  // 获取时间戳，保留小数精度
        // std::cout << std::fixed << std::setprecision(9) << "Image Timestamp: " << tframe << std::endl;

        // 获取当前图像
        cv::Mat leftRGB = camera.cameraL->getIxy();
        cv::Mat rightRGB = camera.cameraR->getIxy();
        cv::flip(leftRGB, leftRGB, 1);
        cv::flip(rightRGB, rightRGB, 1);

        leftRGB.convertTo(leftRGB, CV_8UC1);
        rightRGB.convertTo(rightRGB, CV_8UC1);

        cv::remap(leftRGB, leftRGB, map1_left, map2_left, cv::INTER_LINEAR);
        cv::remap(rightRGB, rightRGB, map1_right, map2_right, cv::INTER_LINEAR);

        // 处理从上一帧到当前帧之间的IMU数据
        std::vector<IMUData> imu_data_in_range;
        
        // 先将上一帧的IMU数据加入
        // imu_data_in_range.push_back(previous_imu_data);

        // 然后筛选当前帧时间段内的IMU数据
        for (auto it = imu_data_buffer.begin(); it != imu_data_buffer.end();) {
            if (it->timestamp >= previous_tframe && it->timestamp < tframe) {
                imu_data_in_range.push_back(*it);
                it = imu_data_buffer.erase(it);  // 删除已处理的数据
            } else {
                ++it;
            }
        }

        // 打印这段时间内的IMU数据
        // for (const auto& imu : imu_data_in_range) {
        //     std::cout << "IMU Data - Timestamp: " << imu.timestamp << std::endl;
        //     std::cout << "Linear Acceleration - x: " << imu.ax << ", y: " << imu.ay << ", z: " << imu.az << std::endl;
        //     std::cout << "Angular Velocity - x: " << imu.gx << ", y: " << imu.gy << ", z: " << imu.gz << std::endl;
        // }
        vImuMeas.clear();

        for(const auto& imu : imu_data_in_range){
            ORB_SLAM3::IMU::Point lastPoint(imu.ax, imu.ay, imu.az,
                                            imu.gx, imu.gy, imu.gz,
                                            imu.timestamp);
            vImuMeas.push_back(lastPoint);
            if(isnan(imu.ax) || isnan(imu.ay) || isnan(imu.az) ||
               isnan(imu.gx) || isnan(imu.gy) || isnan(imu.gz) ||
               isnan(imu.timestamp)){
                exit(-1);
            }
        }

        cout << "Nums: " << vImuMeas.size() << endl;

        // 使用图像和IMU数据进行SLAM处理
        Sophus::SE3f Tcw = SLAM.TrackStereo(leftRGB, rightRGB, tframe);

        // ORB_SLAM3::Atlas* mpAtlas;
        // ORB_SLAM3::Map* pActiveMap = mpAtlas->GetCurrentMap();

        // const vector<ORB_SLAM3::MapPoint*> &vpMPs = pActiveMap->GetAllMapPoints();
        // cout << vpMPs.size() << endl;

        // ROS 回调处理
        ros::spinOnce();

        loop_rate.sleep();

        // 更新前一帧时间戳和上一帧的IMU数据
        previous_tframe = tframe;
        if (!imu_data_in_range.empty()) {
            previous_imu_data = imu_data_in_range.back(); // 将当前帧的最后一个IMU数据作为下一帧的上一帧
        }

    }

    SLAM.Shutdown();
    return 0;
}