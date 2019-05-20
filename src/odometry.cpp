/* includes //{ */

#include <ros/ros.h>
#include <ros/package.h>
#include <nodelet/nodelet.h>
#include <dynamic_reconfigure/server.h>

#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/TwistWithCovarianceStamped.h>
#include <geometry_msgs/AccelStamped.h>
#include <geometry_msgs/Vector3.h>

#include <mavros_msgs/AttitudeTarget.h>

#include <diagnostic_msgs/DiagnosticArray.h>

#include <std_msgs/Bool.h>
#include <std_msgs/Float64.h>

#include <nav_msgs/Odometry.h>
#include <mrs_msgs/EspOdometry.h>

#include <sensor_msgs/Range.h>
#include <sensor_msgs/NavSatFix.h>
#include <sensor_msgs/Imu.h>

#include <std_srvs/Empty.h>
#include <std_srvs/SetBool.h>
#include <std_srvs/Trigger.h>

#include <mrs_msgs/RtkGps.h>
#include <mrs_msgs/TrackerStatus.h>
#include <mrs_msgs/RtkFixType.h>
#include <mrs_msgs/OdometryDiag.h>
#include <mrs_msgs/EstimatorType.h>
#include <mrs_msgs/ChangeEstimator.h>
#include <mrs_msgs/ChangeHdgEstimator.h>
#include <mrs_msgs/Float64Stamped.h>
#include <mrs_msgs/Float64ArrayStamped.h>
#include <mrs_msgs/LkfStates.h>
#include <mrs_msgs/MavrosDiagnostics.h>
#include <mrs_msgs/String.h>
#include <mrs_msgs/OffsetOdom.h>
#include <mrs_msgs/Altitude.h>
#include <mrs_msgs/AltitudeStateNames.h>
#include <mrs_msgs/AltitudeType.h>
#include <mrs_msgs/Heading.h>
#include <mrs_msgs/HeadingStateNames.h>
#include <mrs_msgs/HeadingType.h>

#include <mrs_lib/Profiler.h>
#include <mrs_lib/Lkf.h>
#include <mrs_lib/MedianFilter.h>
#include <mrs_lib/GpsConversions.h>
#include <mrs_lib/ParamLoader.h>

#include <StateEstimator.h>
#include <AltitudeEstimator.h>
#include <HeadingEstimator.h>
#include <StddevBuffer.h>
#include <mrs_odometry/lkfConfig.h>

#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Vector3.h>


#include <string>
#include <locale>
#include <Eigen/Eigen>
#include <math.h>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <queue>

//}

#define STRING_EQUAL 0
#define btoa(x) ((x) ? "true" : "false")
#define NAME_OF(v) #v

namespace mrs_odometry
{

  /* //{ class Odometry */

  class Odometry : public nodelet::Nodelet {

  public:
    virtual void onInit();

  public:
    void publishMessage();  // definition of callback function
    int  rate_;
    bool is_initialized      = false;
    bool is_ready_to_takeoff = false;

  private:
    std::string uav_name;
    bool        simulation_ = false;
    bool        rosbag_     = false;
    bool        use_gt_orientation_;

    bool   _publish_fused_odom;
    bool   _dynamic_optflow_cov       = false;
    double _dynamic_optflow_cov_scale = 0;
    double twist_q_x_prev             = 0;
    double twist_q_y_prev             = 0;

    double _max_optflow_altitude;
    double _max_default_altitude;
    int    _min_satellites;

    ros::NodeHandle nh_;

  private:
    ros::Publisher pub_odom_main_;    // the main fused odometry
    ros::Publisher pub_odom_stable_;  // the main fused odometry
    ros::Publisher pub_slow_odom_;    // the main fused odometry, just slow
    ros::Publisher pub_esp_odom_;
    ros::Publisher pub_rtk_local;
    ros::Publisher pub_rtk_local_odom;
    ros::Publisher pub_gps_local_odom;
    ros::Publisher pub_orientation_gt_;
    ros::Publisher pub_orientation_mavros_;
    ros::Publisher pub_target_attitude_global_;
    ros::Publisher pub_compass_yaw_;
    ros::Publisher pub_odometry_diag_;
    ros::Publisher pub_altitude_;
    ros::Publisher pub_orientation_;
    ros::Publisher pub_max_altitude_;
    ros::Publisher pub_lkf_states_x_;
    ros::Publisher pub_lkf_states_y_;
    ros::Publisher pub_altitude_state_;
    ros::Publisher pub_inno_elevation_;
    ros::Publisher pub_inno_stddev_elevation_;
    ros::Publisher pub_veldiff_stddev_;
    ros::Publisher pub_inno_cov_elevation_;
    ros::Publisher pub_inno_cov_bias_;
    ros::Publisher pub_alt_cov_;

    ros::Publisher pub_debug_optflow_filter;

  private:
    ros::Subscriber sub_global_position_;
    ros::Subscriber sub_tracker_status_;

    // Pixhawk odometry subscriber and callback
    ros::Subscriber sub_pixhawk_;
    ros::Subscriber sub_pixhawk_imu_;
    ros::Subscriber sub_pixhawk_compass_;
    ros::Subscriber sub_optflow_;
    ros::Subscriber sub_optflow_stddev_;
    ros::Subscriber sub_vio_;
    ros::Subscriber sub_t265_odom_;
    ros::Subscriber sub_object_;
    ros::Subscriber rtk_gps_sub_;
    ros::Subscriber sub_icp_relative_;
    ros::Subscriber sub_icp_global_;
    ros::Subscriber sub_target_attitude_;
    ros::Subscriber sub_ground_truth_;
    ros::Subscriber sub_mavros_diagnostic_;
    ros::Subscriber sub_vio_state_;

  private:
    ros::ServiceServer ser_reset_lateral_kalman_;
    ros::ServiceServer ser_offset_odom_;
    ros::ServiceServer ser_teraranger_;
    ros::ServiceServer ser_garmin_;
    ros::ServiceServer ser_toggle_rtk_altitude;
    ros::ServiceServer ser_change_estimator_type;
    ros::ServiceServer ser_change_estimator_type_string;
    ros::ServiceServer ser_change_hdg_estimator_type;
    ros::ServiceServer ser_change_hdg_estimator_type_string;

  private:
    tf2_ros::TransformBroadcaster *             broadcaster_;
    tf2_ros::Buffer                             m_tf_buffer;
    std::unique_ptr<tf2_ros::TransformListener> m_tf_listener_ptr;

    dynamic_reconfigure::Server<mrs_odometry::lkfConfig>               server;
    dynamic_reconfigure::Server<mrs_odometry::lkfConfig>::CallbackType f;

    nav_msgs::Odometry odom_pixhawk;
    nav_msgs::Odometry odom_pixhawk_previous;
    nav_msgs::Odometry odom_pixhawk_shifted;
    nav_msgs::Odometry odom_pixhawk_previous_shifted;

    std::mutex mutex_odom_pixhawk;
    std::mutex mutex_odom_pixhawk_shifted;

    ros::Time  odom_pixhawk_last_update;
    std::mutex mutex_gps_local_odom;

    std::vector<nav_msgs::Odometry> vec_odom_aux;

    geometry_msgs::TwistWithCovarianceStamped optflow_twist;
    std::mutex                                mutex_optflow;
    geometry_msgs::TwistWithCovarianceStamped optflow_twist_previous;
    ros::Time                                 optflow_twist_last_update;
    geometry_msgs::Vector3                    optflow_stddev;
    std::mutex                                mutex_optflow_stddev;
    std::shared_ptr<MedianFilter>             optflow_filter_x;
    std::shared_ptr<MedianFilter>             optflow_filter_y;
    bool                                      _optflow_median_filter;
    int                                       _optflow_filter_buffer_size;
    double                                    _optflow_filter_max_valid;
    double                                    _optflow_filter_max_diff;

    // VIO
    nav_msgs::Odometry odom_vio;
    std::mutex         mutex_odom_vio;
    nav_msgs::Odometry odom_vio_previous;
    ros::Time          odom_vio_last_update;

    // Realsense t265
    nav_msgs::Odometry odom_t265;
    std::mutex         mutex_odom_t265;
    nav_msgs::Odometry odom_t265_previous;
    ros::Time          odom_t265_last_update;

    // Object msgs
    nav_msgs::Odometry odom_object;
    std::mutex         mutex_odom_object;
    nav_msgs::Odometry odom_object_previous;
    ros::Time          odom_object_last_update;
    int                counter_odom_object;

    // IMU msgs
    sensor_msgs::Imu pixhawk_imu;
    sensor_msgs::Imu pixhawk_imu_previous;
    std::mutex       mutex_pixhawk_imu;
    ros::Time        pixhawk_imu_last_update;

    // Compass msgs
    std_msgs::Float64 compass_hdg;
    std_msgs::Float64 compass_hdg_previous;
    double            yaw_previous;
    std::mutex        mutex_compass_hdg;
    ros::Time         compass_hdg_last_update;

    geometry_msgs::Vector3Stamped orientation_mavros;
    geometry_msgs::Vector3Stamped orientation_gt;

    // Target attitude msgs
    mavros_msgs::AttitudeTarget target_attitude;
    mavros_msgs::AttitudeTarget target_attitude_previous;
    ros::Time                   target_attitude_last_update;
    std::mutex                  mutex_target_attitude;

    std::mutex       mutex_rtk;
    mrs_msgs::RtkGps rtk_odom_previous;
    mrs_msgs::RtkGps rtk_odom;
    ros::Time        rtk_last_update;
    bool             _rtk_fuse_sps;


    std::mutex                    mutex_icp;
    nav_msgs::Odometry            icp_odom;
    nav_msgs::Odometry            icp_odom_previous;
    ros::Time                     icp_odom_last_update;
    std::shared_ptr<MedianFilter> icp_vel_filter_x;
    std::shared_ptr<MedianFilter> icp_vel_filter_y;
    bool                          _icp_vel_median_filter;
    int                           _icp_vel_filter_buffer_size;
    double                        _icp_vel_filter_max_valid;
    double                        _icp_vel_filter_max_diff;

    std::mutex         mutex_icp_global;
    nav_msgs::Odometry icp_global_odom;
    nav_msgs::Odometry icp_global_odom_previous;
    ros::Time          icp_global_odom_last_update;

    std::mutex         mutex_ground_truth;
    nav_msgs::Odometry ground_truth;

    mrs_msgs::RtkGps rtk_local_previous;
    mrs_msgs::RtkGps rtk_local;

    bool                     _is_estimator_tmp;
    mrs_msgs::EstimatorType  _estimator_type;
    mrs_msgs::EstimatorType  fallback_object_estimator_type;
    mrs_msgs::EstimatorType  _estimator_type_takeoff;
    std::vector<std::string> _estimator_type_names;
    std::vector<std::string> _altitude_type_names;
    std::string              altitude_estimator_name;
    std::mutex               mutex_estimator_type;

    std::string child_frame_id;
    std::mutex  mutex_child_frame_id;

    bool       got_init_heading = false;
    double     m_init_heading;
    ros::Timer transform_timer;
    int        transform_timer_rate_ = 1;
    void       transformTimer(const ros::TimerEvent &event);

    // | -------------------- message callbacks ------------------- |
    void callbackMavrosOdometry(const nav_msgs::OdometryConstPtr &msg);
    void callbackVioOdometry(const nav_msgs::OdometryConstPtr &msg);
    void callbackT265Odometry(const nav_msgs::OdometryConstPtr &msg);
    void callbackOptflowTwist(const geometry_msgs::TwistWithCovarianceStampedConstPtr &msg);
    void callbackOptflowStddev(const geometry_msgs::Vector3ConstPtr &msg);
    void callbackPixhawkUtm(const sensor_msgs::NavSatFixConstPtr &msg);
    void callbackRtkGps(const mrs_msgs::RtkGpsConstPtr &msg);
    void callbackObjectOdometry(const nav_msgs::OdometryConstPtr &msg);
    void callbackIcpRelative(const nav_msgs::OdometryConstPtr &msg);
    void callbackIcpAbsolute(const nav_msgs::OdometryConstPtr &msg);
    void callbackTargetAttitude(const mavros_msgs::AttitudeTargetConstPtr &msg);
    void callbackGroundTruth(const nav_msgs::OdometryConstPtr &msg);
    void callbackReconfigure(mrs_odometry::lkfConfig &config, uint32_t level);
    void callbackMavrosDiag(const mrs_msgs::MavrosDiagnosticsConstPtr &msg);
    void callbackVioState(const std_msgs::Bool &msg);
    void callbackPixhawkImu(const sensor_msgs::ImuConstPtr &msg);
    void callbackPixhawkCompassHdg(const std_msgs::Float64ConstPtr &msg);

    // | ------------------- service callbacks ------------------- |
    bool callbackToggleTeraranger(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res);
    bool callbackToggleGarmin(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res);
    /* bool callbackToggleRtkHeight(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res); */
    bool callbackChangeEstimator(mrs_msgs::ChangeEstimator::Request &req, mrs_msgs::ChangeEstimator::Response &res);
    bool callbackChangeEstimatorString(mrs_msgs::String::Request &req, mrs_msgs::String::Response &res);
    bool callbackResetEstimator(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res);
    bool callbackChangeHdgEstimator(mrs_msgs::ChangeHdgEstimator::Request &req, mrs_msgs::ChangeHdgEstimator::Response &res);
    bool callbackChangeHdgEstimatorString(mrs_msgs::String::Request &req, mrs_msgs::String::Response &res);

    // | --------------------- helper methods --------------------- |
    bool isReadyToTakeoff();
    void stateEstimatorsPrediction(double x, double y, double dt);
    void stateEstimatorsCorrection(double x, double y, const std::string &measurement_name);
    bool changeCurrentEstimator(const mrs_msgs::EstimatorType &desired_estimator);

    void altitudeEstimatorCorrection(double value, const std::string &measurement_name);
    void altitudeEstimatorCorrection(double value, const std::string &measurement_name, const std::shared_ptr<mrs_odometry::AltitudeEstimator> &estimator);
    bool changeCurrentAltitudeEstimator(const mrs_msgs::AltitudeType &desired_estimator);

    void headingEstimatorsPrediction(const double yaw, const double yaw_rot, const double dt);
    void headingEstimatorsCorrection(const double value, const std::string &measurement_name);
    bool changeCurrentHeadingEstimator(const mrs_msgs::HeadingType &desired_estimator);

    void               getGlobalRot(const geometry_msgs::Quaternion &q_msg, double &rx, double &ry, double &rz);
    bool               isValidType(const mrs_msgs::EstimatorType &type);
    bool               isValidType(const mrs_msgs::HeadingType &type);
    bool               isTimestampOK(const double curr_sec, const double prev_sec);
    std::string        printOdometryDiag();
    bool               stringInVector(const std::string &value, const std::vector<std::string> &vector);
    nav_msgs::Odometry applyOdomOffset(const nav_msgs::Odometry &msg);
    double             unwrap(const double yaw, const double yaw_previous);
    double             wrap(const double angle_in);
    void               setYaw(geometry_msgs::Quaternion &q_msg, const double yaw_in);

    // for keeping new odom
    nav_msgs::Odometry shared_odom;
    std::mutex         mutex_shared_odometry;

    nav_msgs::Odometry rtk_local_odom;
    std::mutex         mutex_rtk_local_odom;

    nav_msgs::Odometry odom_stable;
    tf2::Vector3       m_pos_odom_offset;
    tf2::Quaternion    m_rot_odom_offset;

    // Teraranger altitude subscriber and callback
    ros::Subscriber               sub_terarangerone_;
    sensor_msgs::Range            range_terarangerone_;
    sensor_msgs::Range            range_terarangerone_previous;
    std::mutex                    mutex_range_terarangerone;
    void                          callbackTeraranger(const sensor_msgs::RangeConstPtr &msg);
    std::shared_ptr<MedianFilter> terarangerFilter;
    int                           trg_filter_buffer_size;
    double                        trg_max_valid_altitude;
    double                        trg_filter_max_difference;
    double                        TrgMaxQ, TrgMinQ, TrgQChangeRate;

    // Garmin altitude subscriber and callback
    ros::Subscriber               sub_garmin_;
    sensor_msgs::Range            range_garmin;
    sensor_msgs::Range            range_garmin_previous;
    std::mutex                    mutex_range_garmin;
    void                          callbackGarmin(const sensor_msgs::RangeConstPtr &msg);
    std::shared_ptr<MedianFilter> garminFilter;
    int                           garmin_filter_buffer_size;
    double                        garmin_max_valid_altitude;
    double                        garmin_filter_max_difference;
    ros::Time                     garmin_last_update;
    double                        GarminMaxQ, GarminMinQ, GarminQChangeRate;
    bool                          excessive_tilt = false;
    std::shared_ptr<StddevBuffer> stddev_inno_elevation, stddev_veldiff;

    bool got_odom_pixhawk     = false;
    bool got_odom_t265        = false;
    bool got_optflow          = false;
    bool got_range            = false;
    bool got_pixhawk_utm      = false;
    bool got_rtk              = false;
    bool got_icp              = false;
    bool got_icp_global       = false;
    bool got_target_attitude  = false;
    bool got_vio              = false;
    bool got_object           = false;
    bool got_altitude_sensors = false;
    bool got_lateral_sensors  = false;
    bool got_rtk_fix          = false;
    bool got_pixhawk_imu      = false;
    bool got_compass_hdg      = false;

    int got_icp_counter;
    int got_icp_global_counter;
    int got_rtk_counter;

    bool rtk_odom_initialized = false;

    geometry_msgs::Vector3Stamped target_attitude_global;

    // for setting home position
    bool   use_utm_origin_, use_local_origin_;
    double utm_origin_x_, utm_origin_y_;
    double local_origin_x_, local_origin_y_;
    double land_position_x, land_position_y;
    bool   land_position_set = false;

    // current position in UTM as measure by pixhawk
    double     pixhawk_utm_position_x, pixhawk_utm_position_y;
    std::mutex mutex_pixhawk_utm_position;

    // subscribing to tracker status
    mrs_msgs::TrackerStatus tracker_status;
    std::mutex              mutex_tracker_status;
    void                    callbackTrackerStatus(const mrs_msgs::TrackerStatusConstPtr &msg);
    bool                    got_tracker_status = false;
    bool                    isUavFlying();
    bool                    isUavLandoff();
    bool                    uav_in_the_air = false;
    std::string             null_tracker_;

    // recording the position during landing

    // offset to adjust the local origin
    double pixhawk_odom_offset_x, pixhawk_odom_offset_y;
    bool   got_pixhawk_odom_offset  = false;
    bool   got_pixhawk_odom_shifted = false;

    geometry_msgs::Vector3 mavros_glitch;

    // initial position
    double init_pose_x, init_pose_y, init_pose_z, init_pose_yaw;

    // heading estimation
    int                                                      heading_n, heading_m, heading_p;
    Eigen::MatrixXd                                          A_hdg, B_hdg, R_hdg;
    std::mutex                                               mutex_heading_estimator;
    std::mutex                                               mutex_hdg_estimator_type;
    std::vector<std::string>                                 _heading_estimators_names;
    std::vector<std::string>                                 _hdg_model_state_names;
    std::vector<std::string>                                 _hdg_measurement_names;
    std::map<std::string, std::vector<std::string>>          map_hdg_estimator_measurement;
    std::map<std::string, Eigen::MatrixXd>                   map_hdg_measurement_covariance;
    std::map<std::string, std::string>                       map_hdg_measurement_state;
    std::map<std::string, int>                               map_hdg_measurement_name_id;
    std::map<std::string, Eigen::MatrixXd>                   map_hdg_states;
    std::map<std::string, mrs_msgs::Float64ArrayStamped>     map_hdg_estimator_msg;
    std::map<std::string, ros::Publisher>                    map_hdg_estimator_pub;
    std::map<std::string, std::shared_ptr<HeadingEstimator>> m_heading_estimators;
    std::shared_ptr<HeadingEstimator>                        current_hdg_estimator;
    std::string                                              current_hdg_estimator_name;
    mrs_msgs::HeadingType                                    _hdg_estimator_type;
    mrs_msgs::HeadingType                                    _hdg_estimator_type_takeoff;
    std::vector<std::string>                                 _heading_type_names;
    std::string                                              heading_estimator_name;
    std::mutex                                               mutex_current_hdg_estimator;
    int                                                      _optflow_yaw_rate_filter_buffer_size;
    double                                                   _optflow_yaw_rate_filter_max_valid;
    double                                                   _optflow_yaw_rate_filter_max_diff;
    bool                                                     init_hdg_avg_done;
    int                                                      init_hdg_avg_samples;
    double                                                   init_hdg_avg;

    int    _compass_yaw_filter_buffer_size;
    double _compass_yaw_filter_max_diff;

    std::shared_ptr<MedianFilter> optflow_yaw_rate_filter;
    std::shared_ptr<MedianFilter> compass_yaw_filter;
    int                           compass_inconsistent_samples;
    int                           optflow_inconsistent_samples;
    bool                          is_heading_estimator_initialized = false;
    bool                          _use_heading_estimator;
    bool                          _gyro_fallback;

    // altitude estimation
    int                                                       altitude_n, altitude_m, altitude_p;
    Eigen::MatrixXd                                           A_alt, B_alt, R_alt;
    std::mutex                                                mutex_altitude_estimator;
    std::vector<std::string>                                  _altitude_estimators_names;
    std::vector<std::string>                                  _alt_model_state_names;
    std::vector<std::string>                                  _alt_measurement_names;
    std::map<std::string, std::vector<std::string>>           map_alt_estimator_measurement;
    std::map<std::string, Eigen::MatrixXd>                    map_alt_model;
    std::map<std::string, int>                                map_alt_n_states;
    std::map<std::string, Eigen::MatrixXd>                    map_alt_measurement_covariance;
    std::map<std::string, std::string>                        map_alt_measurement_state;
    std::map<std::string, int>                                map_alt_measurement_name_id;
    std::map<std::string, Eigen::MatrixXd>                    map_alt_states;
    std::map<std::string, mrs_msgs::Float64Stamped>           map_alt_estimator_msg;
    std::map<std::string, ros::Publisher>                     map_alt_estimator_pub;
    std::map<std::string, std::shared_ptr<AltitudeEstimator>> m_altitude_estimators;
    std::shared_ptr<AltitudeEstimator>                        current_alt_estimator;
    std::string                                               current_alt_estimator_name;
    mrs_msgs::AltitudeType                                    _alt_estimator_type;
    mrs_msgs::AltitudeType                                    _alt_estimator_type_takeoff;
    std::mutex                                                mutex_current_alt_estimator;
    bool                                                      is_altitude_estimator_initialized = false;
    int                                                       counter_altitude                  = 0;
    bool                                                      obstacle_detected                 = false;
    bool                                                      bias_baro_estimation_enabled      = false;
    double                                                    _max_vel_baro_offset_estimation;
    double                                                    _elevation_tolerance;
    double                                                    _excessive_tilt;
    int                                                       current_altitude_type;
    bool                                                      estimate_elevation;
    bool                                                      estimate_baro_offset;

    // State estimation
    int                                                    _n_model_states;
    int                                                    _n_model_states_rtk;
    std::vector<std::string>                               _state_estimators_names;
    std::vector<std::string>                               _model_state_names;
    std::vector<std::string>                               _measurement_names;
    std::map<std::string, std::vector<std::string>>        map_estimator_measurement;
    std::map<std::string, Eigen::MatrixXd>                 map_measurement_covariance;
    std::map<std::string, std::string>                     map_measurement_state;
    std::map<std::string, int>                             map_measurement_name_id;
    std::map<std::string, Eigen::MatrixXd>                 map_states;
    std::map<std::string, nav_msgs::Odometry>              map_estimator_odom;
    std::map<std::string, ros::Publisher>                  map_estimator_pub;
    std::map<std::string, std::shared_ptr<StateEstimator>> m_state_estimators;
    std::shared_ptr<StateEstimator>                        current_estimator;
    std::mutex                                             mutex_current_estimator;
    std::string                                            current_estimator_name;

    bool   saturate_mavros_position_;
    double max_mavros_pos_correction;
    double max_vio_pos_correction;
    double max_object_pos_correction;
    double max_rtk_pos_correction;
    double _max_t265_vel;

    int                           lateral_n, lateral_m, lateral_p;
    Eigen::MatrixXd               A_lat, B_lat, R_lat;
    Eigen::MatrixXd               A_lat_rtk, B_lat_rtk, R_lat_rtk, Q_lat_rtk, P_lat_rtk;
    std::shared_ptr<mrs_lib::Lkf> estimator_rtk;
    std::mutex                    mutex_rtk_est;

    bool got_home_position_fix = false;
    bool calculatePixhawkOdomOffset(void);

    bool odometry_published;

    mrs_msgs::MavrosDiagnostics mavros_diag;
    std::mutex                  mutex_mavros_diag;

    // reliability of gps
    double max_altitude;
    bool   gps_reliable       = false;
    bool   _gps_available     = false;
    bool   _vio_available     = false;
    bool   vio_reliable       = false;
    bool   optflow_reliable   = false;
    bool   _optflow_available = false;
    bool   _rtk_available     = false;
    bool   rtk_reliable       = false;
    bool   _t265_available    = false;
    bool   t265_reliable      = false;
    bool   _lidar_available   = false;
    bool   _object_available  = false;
    bool   object_reliable    = false;

    // use differential gps
    bool   use_differential_gps = false;
    bool   pass_rtk_as_odom     = false;
    double max_pos_correction_rate;
    double max_altitude_correction_;

    // disabling teraranger on the flight
    bool teraranger_enabled;
    bool garmin_enabled;

    ros::Timer slow_odom_timer;
    ros::Timer diag_timer;
    ros::Timer lkf_states_timer;
    ros::Timer max_altitude_timer;
    ros::Timer topic_watcher_timer;
    int        slow_odom_rate_;
    int        aux_rate_;
    int        diag_rate_;
    int        lkf_states_rate_;
    int        max_altitude_rate_;
    int        topic_watcher_rate_ = 1;
    void       slowOdomTimer(const ros::TimerEvent &event);
    void       diagTimer(const ros::TimerEvent &event);
    void       lkfStatesTimer(const ros::TimerEvent &event);
    void       rtkRateTimer(const ros::TimerEvent &event);
    void       maxAltitudeTimer(const ros::TimerEvent &event);
    void       topicWatcherTimer(const ros::TimerEvent &event);

    // for fusing rtk altitude
    double trg_z_offset_;
    double garmin_z_offset_;
    double fcu_height_;

  private:
    // for fusing rtk altitude
    bool       rtk_altitude_enabled;
    double     rtk_altitude_integral;
    ros::Timer rtk_rate_timer;
    double     rtkQ;
    double     rtk_max_down_difference_;
    double     rtk_max_abs_difference_;

  private:
    ros::Timer main_timer, aux_timer;
    void       mainTimer(const ros::TimerEvent &event);
    void       auxTimer(const ros::TimerEvent &event);

  private:
    mrs_lib::Profiler *profiler;
    bool               profiler_enabled_ = false;

    // --------------------------------------------------------------
    // |                     dynamic reconfigure                    |
    // --------------------------------------------------------------

    boost::recursive_mutex                      config_mutex_;
    typedef mrs_odometry::lkfConfig             Config;
    typedef dynamic_reconfigure::Server<Config> ReconfigureServer;
    boost::shared_ptr<ReconfigureServer>        reconfigure_server_;
    void                                        drs_callback(mrs_odometry::lkfConfig &config, uint32_t level);
    mrs_odometry::lkfConfig                     last_drs_config;
  };

  //}

  /* //{ onInit() */

  void Odometry::onInit() {

    ros::NodeHandle nh_ = nodelet::Nodelet::getMTPrivateNodeHandle();

    ros::Time::waitForValid();

    ROS_INFO("[Odometry]: initializing");

    mrs_lib::ParamLoader param_loader(nh_, "Odometry");

    param_loader.load_param("enable_profiler", profiler_enabled_);

    param_loader.load_param("uav_name", uav_name);
    param_loader.load_param("null_tracker", null_tracker_);

    odometry_published    = false;
    got_odom_pixhawk      = false;
    got_optflow           = false;
    got_vio               = false;
    got_object            = false;
    got_rtk               = false;
    got_odom_t265         = false;
    got_rtk_fix           = false;
    got_pixhawk_utm       = false;
    got_tracker_status    = false;
    got_home_position_fix = false;
    got_altitude_sensors  = false;
    got_lateral_sensors   = false;
    got_pixhawk_imu       = false;
    got_compass_hdg       = false;

    _is_estimator_tmp = false;
    got_rtk_counter   = 0;

    is_heading_estimator_initialized = false;
    init_hdg_avg_samples             = 0;
    init_hdg_avg                     = 0.0;
    init_hdg_avg_done                = false;

    // got_object_altitude = false;

    pixhawk_utm_position_x = 0;
    pixhawk_utm_position_y = 0;


    // ------------------------------------------------------------------------
    // |                        odometry estimator type                       |
    // ------------------------------------------------------------------------

    /* check estimator type to name conversion //{ */

    // prepare the array of names
    // IMPORTANT, update this with each update of the EstimatorType message
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::OPTFLOW));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::GPS));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::OPTFLOWGPS));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::RTK));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::ICP));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::VIO));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::OBJECT));
    _estimator_type_names.push_back(NAME_OF(mrs_msgs::EstimatorType::T265));

    ROS_WARN("[Odometry]: SAFETY Checking the EstimatorType2Name conversion. If it fails here, you should update the code above this ROS_INFO");
    for (int i = 0; i < mrs_msgs::EstimatorType::TYPE_COUNT; i++) {
      std::size_t found        = _estimator_type_names[i].find_last_of(":");
      _estimator_type_names[i] = _estimator_type_names[i].substr(found + 1);
      ROS_INFO("[Odometry]: _estimator_type[%d]=%s", i, _estimator_type_names[i].c_str());
    }

    // prepare the array of names
    // IMPORTANT, update this with each update of the AltitudeType message
    _altitude_type_names.push_back(NAME_OF(mrs_msgs::AltitudeType::HEIGHT));
    _altitude_type_names.push_back(NAME_OF(mrs_msgs::AltitudeType::ALTITUDE));
    _altitude_type_names.push_back(NAME_OF(mrs_msgs::AltitudeType::ELEVATION));

    ROS_WARN("[Odometry]: SAFETY Checking the AltitudeType2Name conversion. If it fails here, you should update the code above this ROS_INFO");
    for (int i = 0; i < mrs_msgs::AltitudeType::TYPE_COUNT; i++) {
      std::size_t found       = _altitude_type_names[i].find_last_of(":");
      _altitude_type_names[i] = _altitude_type_names[i].substr(found + 1);
      ROS_INFO("[Odometry]: _altitude_type[%d]=%s", i, _altitude_type_names[i].c_str());
    }

    // prepare the array of names
    // IMPORTANT, update this with each update of the HeadingType message
    _heading_type_names.push_back(NAME_OF(mrs_msgs::HeadingType::GYRO));
    _heading_type_names.push_back(NAME_OF(mrs_msgs::HeadingType::COMPASS));
    _heading_type_names.push_back(NAME_OF(mrs_msgs::HeadingType::OPTFLOW));

    ROS_WARN("[Odometry]: SAFETY Checking the HeadingType2Name conversion. If it fails here, you should update the code above this ROS_INFO");
    for (int i = 0; i < mrs_msgs::HeadingType::TYPE_COUNT; i++) {
      std::size_t found      = _heading_type_names[i].find_last_of(":");
      _heading_type_names[i] = _heading_type_names[i].substr(found + 1);
      ROS_INFO("[Odometry]: _heading_type[%d]=%s", i, _heading_type_names[i].c_str());
    }

    //}

    param_loader.load_param("rate", rate_);

    param_loader.load_param("simulation", simulation_);
    param_loader.load_param("rosbag", rosbag_);
    param_loader.load_param("slow_odom_rate", slow_odom_rate_);
    param_loader.load_param("diag_rate", diag_rate_);
    param_loader.load_param("aux_rate", aux_rate_);
    param_loader.load_param("max_altitude_rate", max_altitude_rate_);
    param_loader.load_param("lkf_states_rate", lkf_states_rate_);
    param_loader.load_param("trgFilterBufferSize", trg_filter_buffer_size);
    param_loader.load_param("trgFilterMaxValidAltitude", trg_max_valid_altitude);
    param_loader.load_param("trgFilterMaxDifference", trg_filter_max_difference);

    param_loader.load_param("garminFilterBufferSize", garmin_filter_buffer_size);
    param_loader.load_param("garminFilterMaxValidAltitude", garmin_max_valid_altitude);
    param_loader.load_param("garminFilterMaxDifference", garmin_filter_max_difference);

    /* param_loader.load_param("objectAltitudeBufferSize", object_filter_buffer_size); */
    /* param_loader.load_param("objectAltitudeValidAltitude", object_max_valid_altitude); */
    /* param_loader.load_param("objectAltitudeMaxDifference", object_filter_max_difference); */
    /* param_loader.load_param("altitude/objectQ", objectQ); */
    /* param_loader.load_param("staticObjectHeight", static_object_height_); */
    /* param_loader.load_param("dynamicObjectHeight", dynamic_object_height_); */
    /* param_loader.load_param("mobius_z_offset_", trg_z_offset_); */

    param_loader.load_param("trg_z_offset", trg_z_offset_);
    param_loader.load_param("garmin_z_offset", garmin_z_offset_);
    param_loader.load_param("fcu_height", fcu_height_);

    param_loader.load_param("utm_origin_x", utm_origin_x_);
    param_loader.load_param("utm_origin_y", utm_origin_y_);

    if (utm_origin_x_ == 0 || utm_origin_y_ == 0) {

      use_utm_origin_ = false;

    } else {

      param_loader.load_param("use_utm_origin", use_utm_origin_);

      if (use_utm_origin_)
        ROS_INFO("[Odometry]: SetHomeOnStart enabled");
      else
        ROS_INFO("[Odometry]: SetHomeOnStart disabled");
    }

    param_loader.load_param("use_local_origin", use_local_origin_);
    param_loader.load_param("local_origin_x", local_origin_x_);
    param_loader.load_param("local_origin_y", local_origin_y_);

    if (use_local_origin_ && use_utm_origin_) {
      ROS_ERROR("[Odometry]: Cannot use both 'use_local_origin' and 'use_utm_origin'!!");
      ros::shutdown();
    } else if (!use_local_origin_ && !use_utm_origin_) {
      ROS_ERROR("[Odometry]: Non of 'use_local_origin' and 'use_utm_origin' are true!!");
      ros::shutdown();
    }

    if (use_utm_origin_)
      ROS_INFO("[Odometry]: Setting home position on %.5f %.5f", utm_origin_x_, utm_origin_y_);

    pixhawk_odom_offset_x = 0;
    pixhawk_odom_offset_y = 0;

    // load init pose from config file
    std::string path = ros::package::getPath("mrs_odometry");
    path += "/config/init_pose/init_pose.csv";
    std::ifstream init_pose_file(path, std::ifstream::in);
    if (init_pose_file.is_open()) {
      std::string s0, s1, s2, s3;
      std::getline(init_pose_file, s0, ',');
      std::getline(init_pose_file, s1, ',');
      init_pose_x = std::atof(s1.c_str());
      std::getline(init_pose_file, s2, ',');
      init_pose_y = std::atof(s2.c_str());
      init_pose_file.close();
    } else {
      ROS_ERROR("[Odometry]: Error opening file");
    }

    // Optic flow
    param_loader.load_param("max_optflow_altitude", _max_optflow_altitude);
    param_loader.load_param("max_default_altitude", _max_default_altitude);
    param_loader.load_param("lateral/dynamic_optflow_cov", _dynamic_optflow_cov);
    param_loader.load_param("lateral/dynamic_optflow_cov_scale", _dynamic_optflow_cov_scale);
    optflow_stddev.x = 1.0;
    optflow_stddev.y = 1.0;
    optflow_stddev.z = 1.0;

    // Localization sources availability
    param_loader.load_param("min_satellites", _min_satellites);
    param_loader.load_param("gps_available", _gps_available);
    param_loader.load_param("vio_available", _vio_available);
    param_loader.load_param("optflow_available", _optflow_available);
    param_loader.load_param("rtk_available", _rtk_available);
    param_loader.load_param("t265_available", _t265_available);
    param_loader.load_param("lidar_available", _lidar_available);
    param_loader.load_param("object_available", _object_available);
    gps_reliable        = _gps_available;
    rtk_reliable        = _rtk_available;
    t265_reliable       = _t265_available;
    object_reliable     = false;
    counter_odom_object = false;

    // Takeoff type
    std::string takeoff_estimator;
    param_loader.load_param("takeoff_estimator", takeoff_estimator);

    std::transform(takeoff_estimator.begin(), takeoff_estimator.end(), takeoff_estimator.begin(), ::toupper);
    size_t pos = std::distance(_estimator_type_names.begin(), find(_estimator_type_names.begin(), _estimator_type_names.end(), takeoff_estimator));
    _estimator_type_takeoff.name = takeoff_estimator;
    _estimator_type_takeoff.type = (int)pos;

    /* load parameters of altitude estimator //{ */

    param_loader.load_param("altitude/estimate_elevation", estimate_elevation);
    param_loader.load_param("altitude/estimate_baro_offset", estimate_baro_offset);

    param_loader.load_param("altitude/numberOfVariables", altitude_n);
    param_loader.load_param("altitude/numberOfInputs", altitude_m);
    param_loader.load_param("altitude/numberOfMeasurements", altitude_p);

    param_loader.load_matrix_dynamic("altitude/A", A_alt, altitude_n, altitude_n);
    param_loader.load_matrix_dynamic("altitude/B", B_alt, altitude_n, altitude_m);
    param_loader.load_matrix_dynamic("altitude/R", R_alt, altitude_n, altitude_n);

    param_loader.load_param("altitude/TrgMaxQ", TrgMaxQ);
    param_loader.load_param("altitude/TrgMinQ", TrgMinQ);
    param_loader.load_param("altitude/TrgQChangeRate", TrgQChangeRate);

    param_loader.load_param("altitude/GarminMaxQ", GarminMaxQ);
    param_loader.load_param("altitude/GarminMinQ", GarminMinQ);
    param_loader.load_param("altitude/GarminQChangeRate", GarminQChangeRate);

    param_loader.load_param("altitude_estimators/model_states", _alt_model_state_names);
    param_loader.load_param("altitude_estimators/measurements", _alt_measurement_names);
    param_loader.load_param("altitude_estimators/altitude_estimators", _altitude_estimators_names);

    param_loader.load_param("altitude/max_vel_baro_offset_estimation", _max_vel_baro_offset_estimation);
    param_loader.load_param("altitude/elevation_tolerance", _elevation_tolerance);
    param_loader.load_param("altitude/excessive_tilt", _excessive_tilt);

    param_loader.load_param("altitude/altitude_estimator", altitude_estimator_name);
    size_t pos_alt = std::distance(_altitude_type_names.begin(), find(_altitude_type_names.begin(), _altitude_type_names.end(), altitude_estimator_name));

    _alt_estimator_type_takeoff.name = altitude_estimator_name;
    _alt_estimator_type_takeoff.type = (int)pos_alt;

    /* param_loader.load_param("altitude/rtkQ", rtkQ); */

    // failsafes for altitude fusion
    /* param_loader.load_param("altitude/rtk_max_down_difference", rtk_max_down_difference_); */
    /* param_loader.load_param("altitude/rtk_max_abs_difference", rtk_max_abs_difference_); */

    /* param_loader.load_param("altitude/object_altitude_max_down_difference", object_altitude_max_down_difference_); */
    /* param_loader.load_param("altitude/object_altitude_max_abs_difference", object_altitude_max_abs_difference_); */

    terarangerFilter = std::make_shared<MedianFilter>(trg_filter_buffer_size, trg_max_valid_altitude, 0, trg_filter_max_difference);
    garminFilter     = std::make_shared<MedianFilter>(garmin_filter_buffer_size, garmin_max_valid_altitude, 0, garmin_filter_max_difference);

    stddev_veldiff        = std::make_shared<StddevBuffer>(1000);
    stddev_inno_elevation = std::make_shared<StddevBuffer>(1000);

    ROS_INFO("[Odometry]: Garmin max valid altitude: %2.2f", garmin_max_valid_altitude);
    ROS_INFO("[Odometry]: Teraranger max valid altitude: %2.2f", trg_max_valid_altitude);

    /* objectAltitudeFilter = new MedianFilter(object_filter_buffer_size, 0, false, object_max_valid_altitude, object_filter_max_difference); */

    // Load the measurements fused by each state estimator
    for (std::vector<std::string>::iterator it = _altitude_estimators_names.begin(); it != _altitude_estimators_names.end(); ++it) {

      /* std::string              alt_estimator_name = "ALTITUDE"; */
      std::vector<std::string> temp_vector;
      param_loader.load_param("altitude_estimators/fused_measurements/" + *it, temp_vector);

      for (std::vector<std::string>::iterator it2 = temp_vector.begin(); it2 != temp_vector.end(); ++it2) {
        if (!stringInVector(*it2, _alt_measurement_names)) {
          ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid measurement name!", it2->c_str(), it->c_str());
          ros::shutdown();
        }
      }

      map_alt_estimator_measurement.insert(std::pair<std::string, std::vector<std::string>>(*it, temp_vector));

      // Load the model of each estimator
      int temp_value;
      param_loader.load_param("altitude_estimators/n_A/" + *it, temp_value);
      map_alt_n_states.insert(std::pair<std::string, int>(*it, temp_value));
      Eigen::MatrixXd A_model;
      param_loader.load_matrix_dynamic("altitude_estimators/A/" + *it, A_model, temp_value, temp_value);

      map_alt_model.insert(std::pair<std::string, Eigen::MatrixXd>(*it, A_model));
    }

    // Load the model state of each measurement
    for (std::vector<std::string>::iterator it = _alt_measurement_names.begin(); it != _alt_measurement_names.end(); ++it) {

      std::string temp_value;
      param_loader.load_param("altitude_estimators/measurement_states/" + *it, temp_value);

      if (!stringInVector(temp_value, _alt_model_state_names)) {
        ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid model_state name!", temp_value.c_str(), it->c_str());
        ros::shutdown();
      }

      map_alt_measurement_state.insert(std::pair<std::string, std::string>(*it, temp_value));
    }

    // Load the model state mapping
    for (std::vector<std::string>::iterator it = _alt_model_state_names.begin(); it != _alt_model_state_names.end(); ++it) {

      Eigen::MatrixXd temp_P = Eigen::MatrixXd::Zero(1, altitude_n);
      param_loader.load_matrix_static("altitude_estimators/state_mapping/" + *it, temp_P, 1, altitude_n);

      map_alt_states.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_P));
    }

    // Load the covariances of each measurement
    for (std::vector<std::string>::iterator it = _alt_measurement_names.begin(); it != _alt_measurement_names.end(); ++it) {

      Eigen::MatrixXd temp_matrix;
      param_loader.load_matrix_static("altitude/Q/" + *it, temp_matrix, 1, 1);

      map_alt_measurement_covariance.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_matrix));
    }


    for (std::vector<std::string>::iterator it = _alt_measurement_names.begin(); it < _alt_measurement_names.end(); it++) {
      map_alt_measurement_name_id.insert(std::pair<std::string, int>(*it, (int)std::distance(_alt_measurement_names.begin(), it)));
    }

    //}

    /* create altitude estimator //{ */


    // Loop through all estimators
    for (std::vector<std::string>::iterator it = _altitude_estimators_names.begin(); it != _altitude_estimators_names.end(); ++it) {

      std::vector<bool>            alt_fusing_measurement;
      std::vector<Eigen::MatrixXd> P_arr_alt, Q_arr_alt;

      // Find measurements fused by the estimator
      std::map<std::string, std::vector<std::string>>::iterator temp_vec = map_alt_estimator_measurement.find(*it);

      // Loop through all measurements
      for (std::vector<std::string>::iterator it2 = _alt_measurement_names.begin(); it2 != _alt_measurement_names.end(); ++it2) {

        // Check whether measurement is fused by the estimator
        if (stringInVector(*it2, temp_vec->second)) {
          alt_fusing_measurement.push_back(true);
        } else {
          alt_fusing_measurement.push_back(false);
        }
        ROS_WARN("[Odometry]: estimator: %s measurement: %s fusing: %s", it->c_str(), it2->c_str(), btoa(stringInVector(*it2, temp_vec->second)));

        // Find state name
        std::map<std::string, std::string>::iterator pair_measurement_state = map_alt_measurement_state.find(*it2);

        // Find measurement to state mapping
        std::map<std::string, Eigen::MatrixXd>::iterator pair_state_matrix = map_alt_states.find(pair_measurement_state->second);
        P_arr_alt.push_back(pair_state_matrix->second);

        // Find measurement covariance
        std::map<std::string, Eigen::MatrixXd>::iterator pair_measurement_covariance = map_alt_measurement_covariance.find(*it2);
        Q_arr_alt.push_back(pair_measurement_covariance->second);
      }

      // Find model matrix A
      int                                              n_states             = map_alt_n_states.find(*it)->second;
      Eigen::MatrixXd                                  A_model              = Eigen::MatrixXd::Zero(n_states, n_states);
      std::map<std::string, Eigen::MatrixXd>::iterator pair_estimator_model = map_alt_model.find(*it);
      A_model                                                               = pair_estimator_model->second;

      /* main_altitude_kalman       = new mrs_lib::Lkf(altitude_n, altitude_m, altitude_p, A1, B1, R1, Q1, P1); */
      /* failsafe_teraranger_kalman = new mrs_lib::Lkf(altitude_n, altitude_m, altitude_p, A1, B1, R1, Q1, P1); */
      /* altitude_estimator = std::make_shared<AltitudeEstimator>(alt_estimator_name, alt_fusing_measurement, P_arr_alt, Q_arr_alt, A_alt, B_alt, R_alt); */

      // Add pointer to altitude estimator to array
      m_altitude_estimators.insert(std::pair<std::string, std::shared_ptr<AltitudeEstimator>>(
          *it, std::make_shared<AltitudeEstimator>(*it, alt_fusing_measurement, P_arr_alt, Q_arr_alt, A_model, B_alt, R_alt)));

      // Map odometry to estimator name
      mrs_msgs::Float64Stamped alt_msg;
      std::string              alt_estimator_name = *it;
      std::transform(alt_estimator_name.begin(), alt_estimator_name.end(), alt_estimator_name.begin(), ::tolower);
      /* alt_msg.child_frame_id = alt_estimator_name; */
      map_alt_estimator_msg.insert(std::pair<std::string, mrs_msgs::Float64Stamped>(*it, alt_msg));

      // Map publisher to estimator name
      ros::Publisher pub = nh_.advertise<mrs_msgs::Float64Stamped>(alt_estimator_name + "_out", 1);
      map_alt_estimator_pub.insert(std::pair<std::string, ros::Publisher>(*it, pub));
    }

    ROS_INFO_STREAM("[Odometry]: Altitude estimator was initiated with following parameters: n: "
                    << altitude_n << ", m: " << altitude_m << ", p: " << altitude_p << ", A: " << A_alt << ", B: " << B_alt << ", R: " << R_alt
                    << ", TrgMaxQ: " << TrgMaxQ << ", TrgMinQ: " << TrgMinQ << ", TrgQChangeRate: " << TrgQChangeRate);

    ROS_INFO("[Odometry]: Altitude estimator prepared");

    //}

    /*  //{ load parameters of state estimators */

    param_loader.load_param("lateral/numberOfVariables", lateral_n);
    param_loader.load_param("lateral/numberOfInputs", lateral_m);
    param_loader.load_param("lateral/numberOfMeasurements", lateral_p);

    param_loader.load_param("state_estimators/n_model_states", _n_model_states);
    param_loader.load_param("state_estimators/state_estimators", _state_estimators_names);
    param_loader.load_param("state_estimators/model_states", _model_state_names);
    param_loader.load_param("state_estimators/measurements", _measurement_names);
    param_loader.load_matrix_dynamic("lateral/A", A_lat, _n_model_states, _n_model_states);
    param_loader.load_matrix_dynamic("lateral/B", B_lat, _n_model_states, 1);
    param_loader.load_matrix_dynamic("lateral/R", R_lat, _n_model_states, _n_model_states);

    param_loader.load_matrix_dynamic("lateral/rtk/A", A_lat_rtk, 2, 2);
    param_loader.load_matrix_dynamic("lateral/rtk/B", B_lat_rtk, 2, 2);
    param_loader.load_matrix_dynamic("lateral/rtk/R", R_lat_rtk, 2, 2);
    param_loader.load_matrix_dynamic("lateral/rtk/Q", Q_lat_rtk, 2, 2);
    param_loader.load_matrix_dynamic("lateral/rtk/P", P_lat_rtk, 2, 2);
    param_loader.load_param("lateral/rtk_fuse_sps", _rtk_fuse_sps);

    // Optflow median filter
    param_loader.load_param("lateral/optflow_median_filter", _optflow_median_filter);
    param_loader.load_param("lateral/optflow_filter_buffer_size", _optflow_filter_buffer_size);
    param_loader.load_param("lateral/optflow_filter_max_valid", _optflow_filter_max_valid);
    param_loader.load_param("lateral/optflow_filter_max_diff", _optflow_filter_max_diff);

    optflow_filter_x =
        std::make_shared<MedianFilter>(_optflow_filter_buffer_size, _optflow_filter_max_valid, -_optflow_filter_max_valid, _optflow_filter_max_diff);
    optflow_filter_y =
        std::make_shared<MedianFilter>(_optflow_filter_buffer_size, _optflow_filter_max_valid, -_optflow_filter_max_valid, _optflow_filter_max_diff);

    // ICP median filter
    param_loader.load_param("lateral/icp_vel_median_filter", _icp_vel_median_filter);
    param_loader.load_param("lateral/icp_vel_filter_buffer_size", _icp_vel_filter_buffer_size);
    param_loader.load_param("lateral/icp_vel_filter_max_valid", _icp_vel_filter_max_valid);
    param_loader.load_param("lateral/icp_vel_filter_max_diff", _icp_vel_filter_max_diff);

    icp_vel_filter_x =
        std::make_shared<MedianFilter>(_icp_vel_filter_buffer_size, _icp_vel_filter_max_valid, -_icp_vel_filter_max_valid, _icp_vel_filter_max_diff);
    icp_vel_filter_y =
        std::make_shared<MedianFilter>(_icp_vel_filter_buffer_size, _icp_vel_filter_max_valid, -_icp_vel_filter_max_valid, _icp_vel_filter_max_diff);

    // Load the measurements fused by each state estimator
    for (std::vector<std::string>::iterator it = _state_estimators_names.begin(); it != _state_estimators_names.end(); ++it) {

      std::vector<std::string> temp_vector;
      param_loader.load_param("state_estimators/fused_measurements/" + *it, temp_vector);

      for (std::vector<std::string>::iterator it2 = temp_vector.begin(); it2 != temp_vector.end(); ++it2) {
        if (!stringInVector(*it2, _measurement_names)) {
          ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid measurement name!", it2->c_str(), it->c_str());
          ros::shutdown();
        }
      }

      map_estimator_measurement.insert(std::pair<std::string, std::vector<std::string>>(*it, temp_vector));
    }

    // Load the model state of each measurement
    for (std::vector<std::string>::iterator it = _measurement_names.begin(); it != _measurement_names.end(); ++it) {

      std::string temp_value;
      param_loader.load_param("state_estimators/measurement_states/" + *it, temp_value);

      if (!stringInVector(temp_value, _model_state_names)) {
        ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid model_state name!", temp_value.c_str(), it->c_str());
        ros::shutdown();
      }

      map_measurement_state.insert(std::pair<std::string, std::string>(*it, temp_value));
    }

    // Load the model state mapping
    for (std::vector<std::string>::iterator it = _model_state_names.begin(); it != _model_state_names.end(); ++it) {

      Eigen::MatrixXd temp_P = Eigen::MatrixXd::Zero(1, _n_model_states);
      param_loader.load_matrix_static("state_estimators/state_mapping/" + *it, temp_P, 1, _n_model_states);

      map_states.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_P));
    }

    // Load the covariances of each measurement
    for (std::vector<std::string>::iterator it = _measurement_names.begin(); it != _measurement_names.end(); ++it) {

      Eigen::MatrixXd temp_matrix;
      param_loader.load_matrix_static("lateral/Q/" + *it, temp_matrix, 1, 1);

      map_measurement_covariance.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_matrix));
    }

    for (std::vector<std::string>::iterator it = _measurement_names.begin(); it < _measurement_names.end(); it++) {
      map_measurement_name_id.insert(std::pair<std::string, int>(*it, (int)std::distance(_measurement_names.begin(), it)));
    }

    //}

    /*  //{ create state estimators*/
    for (std::vector<std::string>::iterator it = _state_estimators_names.begin(); it != _state_estimators_names.end(); ++it) {

      std::vector<bool>            fusing_measurement;
      std::vector<Eigen::MatrixXd> P_arr_lat, Q_arr_lat;

      // Find measurements fused by the estimator
      std::map<std::string, std::vector<std::string>>::iterator temp_vec = map_estimator_measurement.find(*it);

      // Loop through all measurements
      for (std::vector<std::string>::iterator it2 = _measurement_names.begin(); it2 != _measurement_names.end(); ++it2) {

        // Check whether measurement is fused by the estimator
        if (stringInVector(*it2, temp_vec->second)) {
          fusing_measurement.push_back(true);
        } else {
          fusing_measurement.push_back(false);
        }

        // Find state name
        std::map<std::string, std::string>::iterator pair_measurement_state = map_measurement_state.find(*it2);

        // Find state mapping
        std::map<std::string, Eigen::MatrixXd>::iterator pair_state_matrix = map_states.find(pair_measurement_state->second);
        P_arr_lat.push_back(pair_state_matrix->second);

        // Find measurement covariance
        std::map<std::string, Eigen::MatrixXd>::iterator pair_measurement_covariance = map_measurement_covariance.find(*it2);
        if (std::strcmp(it2->c_str(), "vel_optflow")) {
          Q_arr_lat.push_back(pair_measurement_covariance->second * 1000);
        } else {
          Q_arr_lat.push_back(pair_measurement_covariance->second);
        }
      }

      // Add pointer to state estimator to array
      // this is how to create shared pointers!!! the correct way
      m_state_estimators.insert(std::pair<std::string, std::shared_ptr<StateEstimator>>(
          *it, std::make_shared<StateEstimator>(*it, fusing_measurement, P_arr_lat, Q_arr_lat, A_lat, B_lat, R_lat)));

      if (std::strcmp(it->c_str(), "RTK")) {
        estimator_rtk = std::make_shared<mrs_lib::Lkf>(2, 2, 2, A_lat_rtk, B_lat_rtk, R_lat_rtk, Q_lat_rtk, P_lat_rtk);
      }

      // Map odometry to estimator name
      nav_msgs::Odometry odom;
      std::string        estimator_name = *it;
      std::transform(estimator_name.begin(), estimator_name.end(), estimator_name.begin(), ::tolower);
      odom.child_frame_id = estimator_name;
      map_estimator_odom.insert(std::pair<std::string, nav_msgs::Odometry>(*it, odom));

      // Map publisher to estimator name
      ros::Publisher pub = nh_.advertise<nav_msgs::Odometry>("odom_" + estimator_name + "_out", 1);
      map_estimator_pub.insert(std::pair<std::string, ros::Publisher>(*it, pub));
    }
    //}

    /* load parameters of heading estimator //{ */

    param_loader.load_param("heading/numberOfVariables", heading_n);
    param_loader.load_param("heading/numberOfInputs", heading_m);
    param_loader.load_param("heading/numberOfMeasurements", heading_p);

    param_loader.load_matrix_dynamic("heading/A", A_hdg, heading_n, heading_n);
    param_loader.load_matrix_dynamic("heading/B", B_hdg, heading_n, heading_m);
    param_loader.load_matrix_dynamic("heading/R", R_hdg, heading_n, heading_n);

    param_loader.load_param("heading_estimators/model_states", _hdg_model_state_names);
    param_loader.load_param("heading_estimators/measurements", _hdg_measurement_names);
    param_loader.load_param("heading_estimators/heading_estimators", _heading_estimators_names);

    param_loader.load_param("heading/optflow_yaw_rate_filter_buffer_size", _optflow_yaw_rate_filter_buffer_size);
    param_loader.load_param("heading/optflow_yaw_rate_filter_max_valid", _optflow_yaw_rate_filter_max_valid);
    param_loader.load_param("heading/optflow_yaw_rate_filter_max_diff", _optflow_yaw_rate_filter_max_diff);

    param_loader.load_param("heading/compass_yaw_filter_buffer_size", _compass_yaw_filter_buffer_size);
    /* param_loader.load_param("heading/compass_yaw_filter_max_valid", _compass_yaw_filter_max_valid); */
    param_loader.load_param("heading/compass_yaw_filter_max_diff", _compass_yaw_filter_max_diff);

    param_loader.load_param("heading/heading_estimator", heading_estimator_name);
    param_loader.load_param("heading/use_heading_estimator", _use_heading_estimator);
    param_loader.load_param("heading/gyro_fallback", _gyro_fallback);

    optflow_yaw_rate_filter = std::make_shared<MedianFilter>(_optflow_yaw_rate_filter_buffer_size, _optflow_yaw_rate_filter_max_valid,
                                                             -_optflow_yaw_rate_filter_max_valid, _optflow_yaw_rate_filter_max_diff);

    compass_yaw_filter           = std::make_shared<MedianFilter>(_compass_yaw_filter_buffer_size, 1000000, -1000000, _compass_yaw_filter_max_diff);
    compass_inconsistent_samples = 0;
    optflow_inconsistent_samples = 0;

    size_t pos_hdg = std::distance(_heading_type_names.begin(), std::find(_heading_type_names.begin(), _heading_type_names.end(), heading_estimator_name));

    _hdg_estimator_type_takeoff.name = heading_estimator_name;
    _hdg_estimator_type_takeoff.type = (int)pos_hdg;

    // Load the measurements fused by each heading estimator
    for (std::vector<std::string>::iterator it = _heading_estimators_names.begin(); it != _heading_estimators_names.end(); ++it) {

      std::vector<std::string> temp_vector;
      param_loader.load_param("heading_estimators/fused_measurements/" + *it, temp_vector);

      for (std::vector<std::string>::iterator it2 = temp_vector.begin(); it2 != temp_vector.end(); ++it2) {
        /* for (int i=0; i<_hdg_measurement_names.size(); i++) { */
        /*   ROS_INFO("[Odometry]: %s", _hdg_measurement_names[i]); */
        /* } */
        if (!stringInVector(*it2, _hdg_measurement_names)) {
          ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid measurement name!", it2->c_str(), it->c_str());
          ros::shutdown();
        }
      }

      map_hdg_estimator_measurement.insert(std::pair<std::string, std::vector<std::string>>(*it, temp_vector));
    }

    // Load the model state of each measurement
    for (std::vector<std::string>::iterator it = _hdg_measurement_names.begin(); it != _hdg_measurement_names.end(); ++it) {

      std::string temp_value;
      param_loader.load_param("heading_estimators/measurement_states/" + *it, temp_value);

      if (!stringInVector(temp_value, _hdg_model_state_names)) {
        ROS_ERROR("[Odometry]: the element '%s' of %s is not a valid model_state name!", temp_value.c_str(), it->c_str());
        ros::shutdown();
      }

      map_hdg_measurement_state.insert(std::pair<std::string, std::string>(*it, temp_value));
    }

    // Load the model state mapping
    for (std::vector<std::string>::iterator it = _hdg_model_state_names.begin(); it != _hdg_model_state_names.end(); ++it) {

      Eigen::MatrixXd temp_P = Eigen::MatrixXd::Zero(1, heading_n);
      param_loader.load_matrix_static("heading_estimators/state_mapping/" + *it, temp_P, 1, heading_n);

      map_hdg_states.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_P));
    }

    // Load the covariances of each measurement
    for (std::vector<std::string>::iterator it = _hdg_measurement_names.begin(); it != _hdg_measurement_names.end(); ++it) {

      Eigen::MatrixXd temp_matrix;
      param_loader.load_matrix_static("heading/Q/" + *it, temp_matrix, 1, 1);

      map_hdg_measurement_covariance.insert(std::pair<std::string, Eigen::MatrixXd>(*it, temp_matrix));
    }

    for (std::vector<std::string>::iterator it = _hdg_measurement_names.begin(); it < _hdg_measurement_names.end(); it++) {
      map_hdg_measurement_name_id.insert(std::pair<std::string, int>(*it, (int)std::distance(_hdg_measurement_names.begin(), it)));
    }
    for (auto &it : map_hdg_measurement_name_id) {
      ROS_INFO("[Odometry]: heading measurement mapping: %s - %d", it.first.c_str(), it.second);
    }


    //}

    /* create heading estimator //{ */

    // Loop through all estimators
    for (std::vector<std::string>::iterator it = _heading_estimators_names.begin(); it != _heading_estimators_names.end(); ++it) {

      std::vector<bool>            hdg_fusing_measurement;
      std::vector<Eigen::MatrixXd> P_arr_hdg, Q_arr_hdg;

      // Find measurements fused by the estimator
      std::map<std::string, std::vector<std::string>>::iterator temp_vec = map_hdg_estimator_measurement.find(*it);

      // Loop through all measurements
      for (std::vector<std::string>::iterator it2 = _hdg_measurement_names.begin(); it2 != _hdg_measurement_names.end(); ++it2) {

        // Check whether measurement is fused by the estimator
        if (stringInVector(*it2, temp_vec->second)) {
          hdg_fusing_measurement.push_back(true);
        } else {
          hdg_fusing_measurement.push_back(false);
        }
        ROS_WARN("[Odometry]: estimator: %s measurement: %s fusing: %s", it->c_str(), it2->c_str(), btoa(stringInVector(*it2, temp_vec->second)));

        // Find state name
        std::map<std::string, std::string>::iterator pair_measurement_state = map_hdg_measurement_state.find(*it2);

        // Find measurement to state mapping
        std::map<std::string, Eigen::MatrixXd>::iterator pair_state_matrix = map_hdg_states.find(pair_measurement_state->second);
        P_arr_hdg.push_back(pair_state_matrix->second);

        // Find measurement covariance
        std::map<std::string, Eigen::MatrixXd>::iterator pair_measurement_covariance = map_hdg_measurement_covariance.find(*it2);
        Q_arr_hdg.push_back(pair_measurement_covariance->second);
      }

      // Add pointer to heading estimator to array
      // this is how to create shared pointers!!! the correct way
      m_heading_estimators.insert(std::pair<std::string, std::shared_ptr<HeadingEstimator>>(
          *it, std::make_shared<HeadingEstimator>(*it, hdg_fusing_measurement, P_arr_hdg, Q_arr_hdg, A_hdg, B_hdg, R_hdg)));

      // Map odometry to estimator name
      mrs_msgs::Float64ArrayStamped hdg_msg;
      std::string                   hdg_estimator_name = *it;
      std::transform(hdg_estimator_name.begin(), hdg_estimator_name.end(), hdg_estimator_name.begin(), ::tolower);
      /* hdg_msg.child_frame_id = hdg_estimator_name; */
      map_hdg_estimator_msg.insert(std::pair<std::string, mrs_msgs::Float64ArrayStamped>(*it, hdg_msg));

      // Map publisher to heading estimator name
      ros::Publisher pub = nh_.advertise<mrs_msgs::Float64ArrayStamped>("hdg_" + hdg_estimator_name + "_out", 1);
      map_hdg_estimator_pub.insert(std::pair<std::string, ros::Publisher>(*it, pub));
    }

    ROS_INFO_STREAM("[Odometry]: heading estimator was initiated with following parameters: n: " << heading_n << ", m: " << heading_m << ", p: " << heading_p
                                                                                                 << ", A: " << A_hdg << ", B: " << B_hdg << ", R: " << R_hdg);

    ROS_INFO("[Odometry]: heading estimator prepared");

    //}

    // use differential gps
    param_loader.load_param("use_differential_gps", use_differential_gps);
    param_loader.load_param("publish_fused_odom", _publish_fused_odom);
    param_loader.load_param("pass_rtk_as_odom", pass_rtk_as_odom);
    param_loader.load_param("max_altitude_correction", max_altitude_correction_);

    param_loader.load_param("lateral/saturate_mavros_position", saturate_mavros_position_);
    param_loader.load_param("lateral/max_mavros_pos_correction", max_mavros_pos_correction);
    param_loader.load_param("lateral/max_vio_pos_correction", max_vio_pos_correction);
    param_loader.load_param("lateral/max_object_pos_correction", max_object_pos_correction);
    param_loader.load_param("lateral/max_rtk_pos_correction", max_rtk_pos_correction);
    param_loader.load_param("lateral/max_t265_vel", _max_t265_vel);

    if (pass_rtk_as_odom && !use_differential_gps) {
      ROS_ERROR("[Odometry]: cant have pass_rtk_as_odom TRUE when use_differential_gps FALSE");
      ros::shutdown();
    }

    ROS_INFO("[Odometry]: Differential GPS %s", use_differential_gps ? "enabled" : "disabled");

    odom_pixhawk_last_update = ros::Time::now();

    teraranger_enabled   = true;
    garmin_enabled       = true;
    rtk_altitude_enabled = false;

    // --------------------------------------------------------------
    // |                         tf listener                        |
    // --------------------------------------------------------------
    m_tf_listener_ptr = std::make_unique<tf2_ros::TransformListener>(m_tf_buffer, "mrs_odometry");

    // --------------------------------------------------------------
    // |                     dynamic reconfigure                    |
    // --------------------------------------------------------------

    reconfigure_server_.reset(new ReconfigureServer(config_mutex_, nh_));
    ReconfigureServer::CallbackType f = boost::bind(&Odometry::callbackReconfigure, this, _1, _2);
    reconfigure_server_->setCallback(f);

    // --------------------------------------------------------------
    // |                          profiler                          |
    // --------------------------------------------------------------

    profiler = new mrs_lib::Profiler(nh_, "Odometry", profiler_enabled_);

    // timer routines
    /* routine_main_timer = profiler->registerRoutine(); */
    /* routine_aux_timer  = profiler->registerRoutine("auxTimer", aux_rate_, 0.002); */


    // --------------------------------------------------------------
    // |                         publishers                         |
    // --------------------------------------------------------------

    /* //{ publishers */
    // publisher for new odometry
    pub_odom_main_             = nh_.advertise<nav_msgs::Odometry>("odom_main_out", 1);
    pub_odom_stable_           = nh_.advertise<nav_msgs::Odometry>("odom_stable_out", 1);
    pub_slow_odom_             = nh_.advertise<nav_msgs::Odometry>("slow_odom_out", 1);
    pub_esp_odom_              = nh_.advertise<mrs_msgs::EspOdometry>("esp_odom_out", 1);
    pub_odometry_diag_         = nh_.advertise<mrs_msgs::OdometryDiag>("odometry_diag_out", 1);
    pub_altitude_              = nh_.advertise<mrs_msgs::Float64Stamped>("altitude_out", 1);
    pub_max_altitude_          = nh_.advertise<mrs_msgs::Float64Stamped>("max_altitude_out", 1);
    pub_orientation_           = nh_.advertise<nav_msgs::Odometry>("orientation_out", 1);
    pub_lkf_states_x_          = nh_.advertise<mrs_msgs::LkfStates>("lkf_states_x_out", 1);
    pub_lkf_states_y_          = nh_.advertise<mrs_msgs::LkfStates>("lkf_states_y_out", 1);
    pub_altitude_state_        = nh_.advertise<mrs_msgs::Altitude>("altitude_state_out", 1);
    pub_inno_elevation_        = nh_.advertise<mrs_msgs::Float64Stamped>("inno_elevation_out", 1);
    pub_inno_stddev_elevation_ = nh_.advertise<mrs_msgs::Float64Stamped>("inno_stddev_elevation_out", 1);
    pub_veldiff_stddev_        = nh_.advertise<mrs_msgs::Float64Stamped>("veldiff_stddev_out", 1);
    pub_inno_cov_elevation_    = nh_.advertise<mrs_msgs::Float64Stamped>("inno_cov_elevation_out", 1);
    pub_inno_cov_bias_         = nh_.advertise<mrs_msgs::Float64Stamped>("inno_cov_bias_out", 1);
    pub_alt_cov_               = nh_.advertise<mrs_msgs::Float64ArrayStamped>("altitude_covariance_out", 1);
    pub_debug_optflow_filter   = nh_.advertise<geometry_msgs::TwistWithCovarianceStamped>("optflow_filtered_out", 1);

    // republisher for rtk local
    pub_rtk_local = nh_.advertise<mrs_msgs::RtkGps>("rtk_local_out", 1);

    // republisher for rtk local odometry (e.g. for rviz)
    pub_rtk_local_odom = nh_.advertise<nav_msgs::Odometry>("rtk_local_odom_out", 1);

    // republisher for gps local odometry (e.g. for rviz)
    pub_gps_local_odom = nh_.advertise<nav_msgs::Odometry>("gps_local_odom_out", 1);

    // publisher for tf
    broadcaster_ = new tf2_ros::TransformBroadcaster();

    pub_compass_yaw_ = nh_.advertise<mrs_msgs::Float64Stamped>("compass_yaw_out", 1);

    // publishers for roll pitch yaw orientations in local_origin frame
    pub_target_attitude_global_ = nh_.advertise<geometry_msgs::Vector3Stamped>("target_attitude_global_out", 1);
    pub_orientation_gt_         = nh_.advertise<geometry_msgs::Vector3Stamped>("orientation_gt_out", 1);
    pub_orientation_mavros_     = nh_.advertise<geometry_msgs::Vector3Stamped>("orientation_mavros_out", 1);
    //}

    // --------------------------------------------------------------
    // |                         subscribers                        |
    // --------------------------------------------------------------

    /* //{ subscribers */
    // subsribe to target attitude
    sub_target_attitude_ = nh_.subscribe("target_attitude_in", 1, &Odometry::callbackTargetAttitude, this, ros::TransportHints().tcpNoDelay());

    // subscribe to pixhawk imu
    sub_pixhawk_imu_ = nh_.subscribe("pixhawk_imu_in", 1, &Odometry::callbackPixhawkImu, this, ros::TransportHints().tcpNoDelay());

    // subscribe to compass heading
    sub_pixhawk_compass_ = nh_.subscribe("pixhawk_compass_in", 1, &Odometry::callbackPixhawkCompassHdg, this, ros::TransportHints().tcpNoDelay());

    // subscriber to mavros odometry
    sub_pixhawk_ = nh_.subscribe("pixhawk_odom_in", 1, &Odometry::callbackMavrosOdometry, this, ros::TransportHints().tcpNoDelay());

    // subscriber to t265 odometry
    sub_t265_odom_ = nh_.subscribe("t265_odom_in", 1, &Odometry::callbackT265Odometry, this, ros::TransportHints().tcpNoDelay());

    // subscriber to optflow velocity
    if (_optflow_available) {
      sub_optflow_        = nh_.subscribe("optflow_in", 1, &Odometry::callbackOptflowTwist, this, ros::TransportHints().tcpNoDelay());
      sub_optflow_stddev_ = nh_.subscribe("optflow_stddev_in", 1, &Odometry::callbackOptflowStddev, this, ros::TransportHints().tcpNoDelay());
    }

    // subscriber to visual odometry
    if (_vio_available) {
      sub_vio_       = nh_.subscribe("vio_in", 1, &Odometry::callbackVioOdometry, this, ros::TransportHints().tcpNoDelay());
      sub_vio_state_ = nh_.subscribe("vio_state_in", 1, &Odometry::callbackVioState, this, ros::TransportHints().tcpNoDelay());
    }

    // subscriber to object odometry
    if (_object_available) {
      sub_object_ = nh_.subscribe("object_in", 1, &Odometry::callbackObjectOdometry, this, ros::TransportHints().tcpNoDelay());
    }

    // subscriber for differential gps
    if (use_differential_gps) {
      rtk_gps_sub_ = nh_.subscribe("rtk_gps_in", 1, &Odometry::callbackRtkGps, this, ros::TransportHints().tcpNoDelay());
    }

    // subscriber for icp odometry
    if (_lidar_available) {
      sub_icp_relative_ = nh_.subscribe("icp_relative_in", 1, &Odometry::callbackIcpRelative, this, ros::TransportHints().tcpNoDelay());
      sub_icp_global_   = nh_.subscribe("icp_absolute_in", 1, &Odometry::callbackIcpAbsolute, this, ros::TransportHints().tcpNoDelay());
    }

    // subscriber for terarangers range
    sub_terarangerone_ = nh_.subscribe("teraranger_in", 1, &Odometry::callbackTeraranger, this, ros::TransportHints().tcpNoDelay());

    // subscriber for garmin range
    sub_garmin_ = nh_.subscribe("garmin_in", 1, &Odometry::callbackGarmin, this, ros::TransportHints().tcpNoDelay());

    // subscriber for ground truth
    sub_ground_truth_ = nh_.subscribe("ground_truth_in", 1, &Odometry::callbackGroundTruth, this, ros::TransportHints().tcpNoDelay());

    // subscribe for utm coordinates
    sub_global_position_ = nh_.subscribe("global_position_in", 1, &Odometry::callbackPixhawkUtm, this, ros::TransportHints().tcpNoDelay());

    // subscribe for tracker status
    sub_tracker_status_ = nh_.subscribe("tracker_status_in", 1, &Odometry::callbackTrackerStatus, this, ros::TransportHints().tcpNoDelay());

    // subscribe for mavros diagnostic
    sub_mavros_diagnostic_ = nh_.subscribe("mavros_diagnostic_in", 1, &Odometry::callbackMavrosDiag, this, ros::TransportHints().tcpNoDelay());
    //}

    // --------------------------------------------------------------
    // |                          services                          |
    // --------------------------------------------------------------

    /* //{ services */

    // subscribe for reset kalman service
    ser_reset_lateral_kalman_ = nh_.advertiseService("reset_lateral_kalman_in", &Odometry::callbackResetEstimator, this);

    // subscribe for garmin toggle service
    ser_garmin_ = nh_.advertiseService("toggle_garmin_in", &Odometry::callbackToggleGarmin, this);

    // toggling fusing of rtk altitude
    /* ser_toggle_rtk_altitude = nh_.advertiseService("toggle_rtk_altitude_in", &Odometry::callbackToggleRtkHeight, this); */

    // change current estimator
    ser_change_estimator_type = nh_.advertiseService("change_estimator_type_in", &Odometry::callbackChangeEstimator, this);

    // change current estimator
    ser_change_estimator_type_string = nh_.advertiseService("change_estimator_type_string_in", &Odometry::callbackChangeEstimatorString, this);

    ser_change_hdg_estimator_type = nh_.advertiseService("change_hdg_estimator_type_in", &Odometry::callbackChangeHdgEstimator, this);

    ser_change_hdg_estimator_type_string = nh_.advertiseService("change_hdg_estimator_type_string_in", &Odometry::callbackChangeHdgEstimatorString, this);

    //}

    // --------------------------------------------------------------
    // |                           timers                           |
    // --------------------------------------------------------------

    /* timers //{ */

    main_timer          = nh_.createTimer(ros::Rate(rate_), &Odometry::mainTimer, this);
    aux_timer           = nh_.createTimer(ros::Rate(aux_rate_), &Odometry::auxTimer, this);
    slow_odom_timer     = nh_.createTimer(ros::Rate(slow_odom_rate_), &Odometry::slowOdomTimer, this);
    rtk_rate_timer      = nh_.createTimer(ros::Rate(1), &Odometry::rtkRateTimer, this);
    diag_timer          = nh_.createTimer(ros::Rate(diag_rate_), &Odometry::diagTimer, this);
    lkf_states_timer    = nh_.createTimer(ros::Rate(lkf_states_rate_), &Odometry::lkfStatesTimer, this);
    max_altitude_timer  = nh_.createTimer(ros::Rate(max_altitude_rate_), &Odometry::maxAltitudeTimer, this);
    topic_watcher_timer = nh_.createTimer(ros::Rate(topic_watcher_rate_), &Odometry::topicWatcherTimer, this);
    transform_timer     = nh_.createTimer(ros::Rate(transform_timer_rate_), &Odometry::transformTimer, this);

    //}

    /* check validity and set takeoff estimator //{ */

    // If required sensor is not available shutdown
    ROS_INFO_ONCE("[Odometry]: Requested %s type for takeoff.", _estimator_type_takeoff.name.c_str());
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OPTFLOW && !_optflow_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Optflow localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::GPS && !_gps_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. GPS localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OPTFLOWGPS && !_optflow_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Optflow localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::RTK && !_rtk_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. RTK localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::T265 && !_t265_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. T265 localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::ICP && !_lidar_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Lidar localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::VIO && !_vio_available) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Visual odometry localization not available. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OBJECT) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Takeoff in this odometry mode is not supported. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }

    bool success;

    success = changeCurrentHeadingEstimator(_hdg_estimator_type_takeoff);
    if (!success) {
      ROS_ERROR("[Odometry]: The takeoff heading estimator type %s could not be set. Shutting down.", _hdg_estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }

    success = changeCurrentAltitudeEstimator(_alt_estimator_type_takeoff);
    if (!success) {
      ROS_ERROR("[Odometry]: The takeoff altitude estimator type %s could not be set. Shutting down.", _alt_estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }

    success = changeCurrentEstimator(_estimator_type_takeoff);
    if (!success) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Shutting down.", _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }

    ROS_INFO("[Odometry]: %s", printOdometryDiag().c_str());

    //}

    /* pass current covariances to dynamic reconfigure //{ */

    current_estimator->getQ(last_drs_config.Q_pos_mavros, map_measurement_name_id.find("pos_mavros")->second);
    current_estimator->getQ(last_drs_config.Q_pos_vio, map_measurement_name_id.find("pos_vio")->second);
    current_estimator->getQ(last_drs_config.Q_pos_object, map_measurement_name_id.find("pos_object")->second);
    current_estimator->getQ(last_drs_config.Q_pos_icp, map_measurement_name_id.find("pos_icp")->second);
    current_estimator->getQ(last_drs_config.Q_pos_rtk, map_measurement_name_id.find("pos_rtk")->second);
    current_estimator->getQ(last_drs_config.Q_vel_mavros, map_measurement_name_id.find("vel_mavros")->second);
    current_estimator->getQ(last_drs_config.Q_vel_vio, map_measurement_name_id.find("vel_vio")->second);
    current_estimator->getQ(last_drs_config.Q_vel_icp, map_measurement_name_id.find("vel_icp")->second);
    current_estimator->getQ(last_drs_config.Q_vel_optflow, map_measurement_name_id.find("vel_optflow")->second);
    current_estimator->getQ(last_drs_config.Q_vel_rtk, map_measurement_name_id.find("vel_rtk")->second);
    current_estimator->getQ(last_drs_config.Q_tilt, map_measurement_name_id.find("tilt_mavros")->second);

    current_alt_estimator->getQ(last_drs_config.Q_alt_baro, map_alt_measurement_name_id.find("alt_baro")->second);
    current_alt_estimator->getQ(last_drs_config.Q_z_vel_mavros, map_alt_measurement_name_id.find("vel_mavros")->second);
    current_alt_estimator->getQ(last_drs_config.Q_height_range, map_alt_measurement_name_id.find("height_range")->second);
    current_alt_estimator->getQ(last_drs_config.Q_bias_baro, map_alt_measurement_name_id.find("bias_baro")->second);
    current_alt_estimator->getQ(last_drs_config.Q_elevation, map_alt_measurement_name_id.find("elevation")->second);

    current_hdg_estimator->getQ(last_drs_config.Q_yaw_compass, map_hdg_measurement_name_id.find("yaw_compass")->second);
    current_hdg_estimator->getQ(last_drs_config.Q_rate_gyro, map_hdg_measurement_name_id.find("rate_gyro")->second);
    current_hdg_estimator->getQ(last_drs_config.Q_rate_optflow, map_hdg_measurement_name_id.find("rate_optflow")->second);

    reconfigure_server_->updateConfig(last_drs_config);

    //}

    // | ----------------------- finish init ---------------------- |
    if (!param_loader.loaded_successfully()) {
      ROS_ERROR("[Odometry]: Could not load all non-optional parameters. Shutting down.");
      ros::shutdown();
    }

    is_initialized = true;

    ROS_INFO("[Odometry]: initialized");
  }

  //}

  /* isReadyToTakeoff() //{ */

  bool Odometry::isReadyToTakeoff() {

    // Wait for necessary msgs
    ROS_INFO_ONCE("[Odometry]: Requested %s type for takeoff.", _estimator_type_takeoff.name.c_str());
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OPTFLOW) {
      if (got_optflow) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for optic flow msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::GPS) {
      if (got_odom_pixhawk) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for pixhawk msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OPTFLOWGPS) {
      if (got_optflow && got_odom_pixhawk) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for optic flow and pixhawk msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::RTK) {
      // return true, since RTK can work even with normal GPS
      return true;
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::T265) {
      if (got_odom_t265) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for T265 msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::ICP) {
      if (got_icp) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for icp msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::VIO) {
      if (got_vio) {
        return true;
      } else {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for vio msg to initialize takeoff estimator");
        return false;
      }
    }
    if (_estimator_type_takeoff.type == mrs_msgs::EstimatorType::OBJECT) {
      ROS_ERROR("[Odometry]: The takeoff odometry type %s could not be set. Takeoff in this odometry mode is not supported. Shutting down.",
                _estimator_type_takeoff.name.c_str());
      ros::shutdown();
    }
    return false;
  }

  //}

  /* //{ isUavFlying() */

  bool Odometry::isUavFlying() {

    std::scoped_lock lock(mutex_tracker_status);

    if (got_tracker_status) {

      if (std::string(tracker_status.tracker).compare("mrs_mav_manager/NullTracker") == STRING_EQUAL) {

        return false;
      } else {

        return true;
      }

    } else {

      return false;
    }
  }

  //}

  /* //{ isUavLandoff() */

  bool Odometry::isUavLandoff() {

    std::scoped_lock lock(mutex_tracker_status);

    if (got_tracker_status) {

      if (std::string(tracker_status.tracker).compare("mrs_mav_manager/LandoffTracker") == STRING_EQUAL) {

        return true;
      } else {

        return false;
      }

    } else {

      return false;
    }
  }

  //}

  // --------------------------------------------------------------
  // |                           timers                           |
  // --------------------------------------------------------------

  /* //{ mainTimer() */

  void Odometry::mainTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("mainTimer", rate_, 0.004, event);

    // Just return without publishing - the t265 odometry is republished in callback at faster rate
    if (std::strcmp(current_estimator_name.c_str(), "T265") == STRING_EQUAL) {

      return;
    }

    // --------------------------------------------------------------
    // |              publish the new altitude message              |
    // --------------------------------------------------------------

    if (!got_odom_pixhawk || !got_range) {
      ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for altitude data from sensors - received? pixhawk: %s, ranger: %s", got_odom_pixhawk ? "TRUE" : "FALSE",
                        got_range ? "TRUE" : "FALSE");
      return;
    }

    /* publish altitude  //{ */

    got_altitude_sensors = true;

    mrs_msgs::Float64Stamped new_altitude;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);

      new_altitude.header = odom_pixhawk.header;
      new_altitude.value  = odom_pixhawk.pose.pose.position.z;
    }

    new_altitude.header.frame_id = "local_origin";
    new_altitude.header.stamp    = ros::Time::now();

    // update the altitude state
    Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(altitude_n, 1);
    {
      std::scoped_lock lock(mutex_altitude_estimator);
      /* new_altitude.value = main_altitude_kalman->getState(0); */
      if (!current_alt_estimator->getStates(current_altitude)) {
        ROS_WARN("[Odometry]: Altitude estimator not initialized.");
        return;
      }
      /* ROS_WARN_STREAM_THROTTLE(1.0, "[Odometry]: altitude states:" << std::endl << current_altitude); */
      if (_alt_estimator_type.type == mrs_msgs::AltitudeType::ALTITUDE || _alt_estimator_type.type == mrs_msgs::AltitudeType::ELEVATION) {
        new_altitude.value = current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);
      } else if (_alt_estimator_type.type == mrs_msgs::AltitudeType::HEIGHT) {
        new_altitude.value = current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT);
      } else {
        ROS_ERROR_THROTTLE(1.0, "[Odometry]: unknown altitude type: %d", _alt_estimator_type.type);
      }
      /* ROS_WARN_THROTTLE(1.0, "[Odometry]: Publishing altitude from estimator type: %d", _alt_estimator_type.type); */
    }

    /* if (fabs(main_altitude_kalman->getState(0) - failsafe_teraranger_kalman->getState(0)) > 0.5) { */
    /*   ROS_WARN_THROTTLE(1.0, "[Odometry]: Main altitude: %2.2f, Failsafe altitude: %2.2f", main_altitude_kalman->getState(0), */
    /*                     failsafe_teraranger_kalman->getState(0)); */
    /* } */
    try {
      pub_altitude_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(new_altitude)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_altitude_.getTopic().c_str());
    }

    //}

    /* publish altitude state //{ */

    mrs_msgs::Altitude altitude_state;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      altitude_state.header = odom_pixhawk.header;
    }
    altitude_state.header.stamp    = ros::Time::now();
    altitude_state.header.frame_id = "local_origin";
    altitude_state.altitude        = current_altitude(0);
    altitude_state.vel_alt         = current_altitude(1);
    altitude_state.height          = current_altitude(2);
    altitude_state.vel_height      = current_altitude(3);
    altitude_state.elevation       = current_altitude(4);
    altitude_state.baro_offset     = current_altitude(5);

    try {
      pub_altitude_state_.publish(mrs_msgs::AltitudeConstPtr(new mrs_msgs::Altitude(altitude_state)));
      ROS_INFO_ONCE("[Odometry]: Publishing altitude");
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_altitude_state_.getTopic().c_str());
    }

    //}

    if (_use_heading_estimator && !init_hdg_avg_done && std::strcmp(current_hdg_estimator_name.c_str(), "COMPASS") == STRING_EQUAL) {
      ROS_INFO_THROTTLE(1.0, "[Odometry]: Waiting for averaging of initial heading.");
      return;
    }

    if (!is_heading_estimator_initialized) {

      Eigen::VectorXd yaw       = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd yaw_rate  = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd gyro_bias = Eigen::VectorXd::Zero(1);
      Eigen::MatrixXd init_cov  = Eigen::MatrixXd::Identity(heading_n, heading_n);
      init_cov *= 1000;
      yaw_rate << 0.0;
      gyro_bias << 0.0;
      yaw << init_hdg_avg;

      // Initialize all altitude estimators
      for (auto &estimator : m_heading_estimators) {
        estimator.second->setState(0, yaw);
        estimator.second->setState(1, yaw_rate);
        estimator.second->setState(2, gyro_bias);
        estimator.second->setCovariance(init_cov);
      }
      is_heading_estimator_initialized = true;
    }

    /* publish heading  //{ */

    nav_msgs::Odometry orientation;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      orientation.header                = odom_pixhawk.header;
      orientation.pose.pose.orientation = odom_pixhawk.pose.pose.orientation;
    }
    if (_use_heading_estimator) {

      Eigen::VectorXd yaw(1);
      Eigen::VectorXd yaw_rate(1);

      {
        std::scoped_lock lock(mutex_current_hdg_estimator);

        current_hdg_estimator->getState(0, yaw);
        current_hdg_estimator->getState(1, yaw_rate);
      }
      yaw(0) = wrap(yaw(0));
      setYaw(orientation.pose.pose.orientation, yaw(0));
      /* odom_main.twist.twist.angular.z = yaw_rate(0); */
      orientation.header.frame_id = "local_origin";
      orientation.child_frame_id  = current_hdg_estimator->getName();
    }

    try {
      pub_orientation_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(orientation)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_orientation_.getTopic().c_str());
    }

    //}

    /* sensor checking //{ */

    if (!is_ready_to_takeoff) {
      is_ready_to_takeoff = isReadyToTakeoff();
      return;
    }

    // Fallback from RTK
    if (_estimator_type.type == mrs_msgs::EstimatorType::RTK) {
      if (!gps_reliable && _optflow_available && got_optflow && current_altitude(0) < _max_optflow_altitude) {
        ROS_WARN("[Odometry]: RTK not reliable. Switching to OPTFLOW type.");
        mrs_msgs::EstimatorType optflow_type;
        optflow_type.type = mrs_msgs::EstimatorType::OPTFLOW;
        changeCurrentEstimator(optflow_type);
      }
      if (!got_odom_pixhawk || !got_range || (use_utm_origin_ && !got_pixhawk_utm)) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, global position: %s, rtk: %s",
                          got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_pixhawk_utm ? "TRUE" : "FALSE", got_rtk ? "TRUE" : "FALSE");
        return;
      }

      // Fallback from GPS
    } else if (_estimator_type.type == mrs_msgs::EstimatorType::GPS) {
      if (!gps_reliable && _optflow_available && got_optflow && current_altitude(2) < _max_optflow_altitude) {
        ROS_WARN("[Odometry]: GPS not reliable. Switching to OPTFLOW type.");
        mrs_msgs::EstimatorType optflow_type;
        optflow_type.type = mrs_msgs::EstimatorType::OPTFLOW;
        changeCurrentEstimator(optflow_type);
      }
      if (!got_odom_pixhawk || !got_range || (use_utm_origin_ && !got_pixhawk_utm)) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, global position: %s",
                          got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_pixhawk_utm ? "TRUE" : "FALSE");
        return;
      }

      // Fallback from OPTFLOWGPS
    } else if (_estimator_type.type == mrs_msgs::EstimatorType::OPTFLOWGPS) {
      if (!gps_reliable && _optflow_available && current_altitude(2) < _max_optflow_altitude) {
        ROS_WARN("[Odometry]: GPS not reliable. Switching to OPTFLOW type.");
        mrs_msgs::EstimatorType optflow_type;
        optflow_type.type = mrs_msgs::EstimatorType::OPTFLOW;
        changeCurrentEstimator(optflow_type);
      }
      if (!got_odom_pixhawk || !got_range || (use_utm_origin_ && !got_pixhawk_utm) || !got_optflow) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, global position: %s, optflow: %s",
                          got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_pixhawk_utm ? "TRUE" : "FALSE",
                          got_optflow ? "TRUE" : "FALSE");
        return;
      }


      // Fallback from T265
    } else if (_estimator_type.type == mrs_msgs::EstimatorType::T265) {
      if ((!got_odom_t265 || !t265_reliable) && _optflow_available && got_optflow && current_altitude(0) < _max_optflow_altitude) {
        ROS_WARN("[Odometry]: T265 not reliable. Switching to OPTFLOW type.");
        mrs_msgs::EstimatorType optflow_type;
        optflow_type.type = mrs_msgs::EstimatorType::OPTFLOW;
        changeCurrentEstimator(optflow_type);
      } else if ((!got_odom_t265 || !t265_reliable) && gps_reliable && got_odom_pixhawk) {
        ROS_WARN("[Odometry]: T265 not reliable. Switching to GPS type.");
        mrs_msgs::EstimatorType gps_type;
        gps_type.type = mrs_msgs::EstimatorType::GPS;
        changeCurrentEstimator(gps_type);
      }
      if (!got_odom_pixhawk || !got_range || (use_utm_origin_ && !got_pixhawk_utm) || !got_odom_t265) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, global position: %s, t265: %s",
                          got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_pixhawk_utm ? "TRUE" : "FALSE",
                          got_odom_t265 ? "TRUE" : "FALSE");
        return;
      }

      // Fallback from OBJECT
    } else if (_estimator_type.type == mrs_msgs::EstimatorType::OBJECT) {
      if (!got_object || !object_reliable) {
        ROS_WARN("[Odometry]: OBJECT not reliable. Switching to %s type.", _estimator_type_names[fallback_object_estimator_type.type].c_str());
        changeCurrentEstimator(fallback_object_estimator_type);
      }

      // Fallback from OPTFLOW
    } else if (_estimator_type.type == mrs_msgs::EstimatorType::OPTFLOW) {
      if (gps_reliable) {

        if (!optflow_reliable) {
          ROS_WARN("[Odometry]: OPTFLOW not reliable. Switching to GPS type.");
          mrs_msgs::EstimatorType gps_type;
          gps_type.type = mrs_msgs::EstimatorType::GPS;
          changeCurrentEstimator(gps_type);
        } else if (!got_odom_pixhawk || !got_range || (use_utm_origin_ && !got_pixhawk_utm) || !got_optflow) {
          ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, global position: %s, optflow: %s",
                            got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_pixhawk_utm ? "TRUE" : "FALSE",
                            got_optflow ? "TRUE" : "FALSE");
          return;
        }

      } else {
        if (!got_odom_pixhawk || !got_range || !got_optflow) {
          ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, optflow: %s",
                            got_odom_pixhawk ? "TRUE" : "FALSE", got_range ? "TRUE" : "FALSE", got_optflow ? "TRUE" : "FALSE");
          return;
        }
      }

    } else if (_estimator_type.type == mrs_msgs::EstimatorType::ICP) {
      if (!got_odom_pixhawk || !got_range || !got_icp) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, icp: %s", got_odom_pixhawk ? "TRUE" : "FALSE",
                          got_range ? "TRUE" : "FALSE", got_icp ? "TRUE" : "FALSE");
        return;
      }

    } else if (_estimator_type.type == mrs_msgs::EstimatorType::VIO) {
      if (!got_odom_pixhawk || !got_range || !got_vio) {
        ROS_INFO_THROTTLE(1, "[Odometry]: Waiting for data from sensors - received? pixhawk: %s, ranger: %s, vio: %s", got_odom_pixhawk ? "TRUE" : "FALSE",
                          got_range ? "TRUE" : "FALSE", got_vio ? "TRUE" : "FALSE");
        return;
      }
    } else {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Unknown odometry type. Not checking sensors.");
    }

    got_lateral_sensors = true;
    ROS_INFO_ONCE("[Odometry]: Lateral sensors ready");


    //}

    /* publish fused odometry //{ */

    {
      std::scoped_lock lock(mutex_odom_pixhawk);

      if ((ros::Time::now() - odom_pixhawk_last_update).toSec() > 0.1) {

        ROS_ERROR("[Odometry]: mavros odometry has not come for > 0.1 s, interrupting");
        got_odom_pixhawk = false;

        return;
      }
    }

    // blocking/returning when cannot calculate utm_origin_offset
    if (!calculatePixhawkOdomOffset()) {
      return;
    }

    if (!got_pixhawk_odom_shifted) {
      return;
    }

    //
    nav_msgs::Odometry odom_main;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      odom_main = odom_pixhawk;
    }

    odom_main.header.frame_id = "local_origin";
    odom_main.header.stamp    = ros::Time::now();

    geometry_msgs::PoseStamped newPose;
    newPose.header = odom_main.header;

    // update the altitude state
    {
      std::scoped_lock lock(mutex_altitude_estimator);

      if (_alt_estimator_type.type == mrs_msgs::AltitudeType::ALTITUDE || _alt_estimator_type.type == mrs_msgs::AltitudeType::ELEVATION) {
        odom_main.pose.pose.position.z = current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);
      } else if (_alt_estimator_type.type == mrs_msgs::AltitudeType::HEIGHT) {
        odom_main.pose.pose.position.z = current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT);
      } else {
        ROS_ERROR_THROTTLE(1.0, "[Odometry]: unknown altitude type: %d", _alt_estimator_type.type);
      }
    }

    // if odometry has not been published yet, initialize lateralKF
    if (!odometry_published) {
      ROS_INFO("[Odometry]: Initializing the states of all estimators");
      if (_estimator_type.type == mrs_msgs::EstimatorType::OPTFLOW && use_local_origin_) {
        Eigen::VectorXd state(2);
        state << local_origin_x_, local_origin_y_;
        current_estimator->setState(0, state);
      } else {
        Eigen::VectorXd state(2);
        double          pos_x = odom_pixhawk_shifted.pose.pose.position.x;
        double          pos_y = odom_pixhawk_shifted.pose.pose.position.y;
        state << pos_x, pos_y;
        /* current_estimator->setState(0, state); */
        for (auto &estimator : m_state_estimators) {
          estimator.second->setState(0, state);
        }
        {
          std::scoped_lock lock(mutex_rtk_est);

          estimator_rtk->setStates(state);
        }
      }
      ROS_INFO("[Odometry]: Initialized the states of all estimators");
      odometry_published = true;
    }

    if (_publish_fused_odom) {

      Eigen::VectorXd pos_vec(2);
      Eigen::VectorXd vel_vec(2);

      {
        std::scoped_lock lock(mutex_current_estimator);

        current_estimator->getState(0, pos_vec);
        current_estimator->getState(1, vel_vec);
        odom_main.child_frame_id = current_estimator->getName();
        if (std::strcmp(current_estimator->getName().c_str(), "OBJECT") == STRING_EQUAL) {
          odom_main.child_frame_id += odom_object.child_frame_id;
        }
      }

      if (_use_heading_estimator) {

        Eigen::VectorXd yaw(1);
        Eigen::VectorXd yaw_rate(1);

        {
          std::scoped_lock lock(mutex_current_hdg_estimator);

          current_hdg_estimator->getState(0, yaw);
          current_hdg_estimator->getState(1, yaw_rate);
        }
        yaw(0) = wrap(yaw(0));
        setYaw(odom_main.pose.pose.orientation, yaw(0));
        odom_main.twist.twist.angular.z = yaw_rate(0);
        odom_main.child_frame_id += "_" + current_hdg_estimator->getName();
      }

      if (std::strcmp(current_estimator_name.c_str(), "RTK") == STRING_EQUAL) {
        {
          std::scoped_lock lock(mutex_rtk_est);

          odom_main.pose.pose.position.x = estimator_rtk->getState(0);
          odom_main.pose.pose.position.y = estimator_rtk->getState(1);
        }
      } else {
        odom_main.pose.pose.position.x = pos_vec(0);
        odom_main.pose.pose.position.y = pos_vec(1);
      }

      if (std::strcmp(current_estimator->getName().c_str(), "RTK") != STRING_EQUAL &&
          std::strcmp(current_estimator->getName().c_str(), "GPS") != STRING_EQUAL) {
        odom_main.twist.twist.linear.x = vel_vec(0);
        odom_main.twist.twist.linear.y = vel_vec(1);
      }

      if (!odometry_published) {
        odom_stable                         = odom_main;
        odom_stable.pose.pose.orientation.x = 0.0;
        odom_stable.pose.pose.orientation.y = 0.0;
        odom_stable.pose.pose.orientation.z = 0.0;
        odom_stable.pose.pose.orientation.w = 1.0;
        m_pos_odom_offset.setZero();
        m_rot_odom_offset = tf2::Quaternion(0.0, 0.0, 0.0, 1.0);
        m_rot_odom_offset.normalize();
      }

      /* } else { */

      /*   odom_main.pose.pose.position.x += pixhawk_odom_offset_x; */
      /*   odom_main.pose.pose.position.y += pixhawk_odom_offset_y; */
      if (std::strcmp(odom_main.child_frame_id.c_str(), odom_stable.child_frame_id.c_str()) != STRING_EQUAL) {

        tf2::Vector3 v1, v2;
        tf2::fromMsg(odom_main.pose.pose.position, v1);
        tf2::fromMsg(odom_stable.pose.pose.position, v2);
        tf2::Vector3 pos_diff = v1 - v2;
        m_pos_odom_offset     = pos_diff;

        // Somehow the odom_stable quaternion becomes (0.0, 0.0, 0.0, 0.0)
        if (odom_stable.pose.pose.orientation.w == 0.0) {
          /* odom_stable.pose.pose.orientation.w = 1.0; */
          odom_stable.pose.pose.orientation = odom_pixhawk.pose.pose.orientation;
        }
        tf2::Quaternion q1, q2;
        tf2::fromMsg(odom_main.pose.pose.orientation, q1);
        tf2::fromMsg(odom_stable.pose.pose.orientation, q2);
        tf2::Quaternion rot_diff = q2 * q1.inverse();
        m_rot_odom_offset        = rot_diff;
        m_rot_odom_offset.normalize();
        /* ROS_WARN("[Odometry]: odometry change stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
         * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
        /* ROS_WARN("[Odometry]: q1: %f, %f, %f, %f,\t q2: %f, %f, %f, %f", q1.x(), q1.y(), q1.z(), q1.w(), q2.x(), q2.y(), q2.z(), q2.w()); */
        ROS_WARN("[Odometry]: Changed odometry estimator. Updating offset for stable odometry.");
      }

      /* ROS_WARN("[Odometry]: before stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
       * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
      odom_stable = applyOdomOffset(odom_main);
      /* ROS_WARN("[Odometry]: after stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
       * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
      odom_stable.header.frame_id = "local_origin_stable";

      try {
        pub_odom_stable_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(odom_stable)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odom_stable_.getTopic().c_str());
      }

      // publish TF
      geometry_msgs::TransformStamped tf;
      tf.header.stamp          = ros::Time::now();
      tf.header.frame_id       = "local_origin_stable";
      tf.child_frame_id        = "local_origin";
      tf.transform.translation = tf2::toMsg(tf2::Vector3(0.0, 0.0, 0.0) - m_pos_odom_offset);
      tf.transform.rotation    = tf2::toMsg(m_rot_odom_offset.inverse());
      try {
        broadcaster_->sendTransform(tf);
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing TF: %s - %s.", tf.child_frame_id.c_str(), tf.header.frame_id.c_str());
      }
    }

    // publish the odometry
    if (pass_rtk_as_odom) {
      {
        std::scoped_lock lock(mutex_rtk_local_odom);

        odom_main = rtk_local_odom;
      }
    }

    {
      std::scoped_lock lock(mutex_shared_odometry);

      shared_odom = odom_main;
    }

    try {
      pub_odom_main_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(odom_main)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odom_main_.getTopic().c_str());
    }
    ROS_INFO_ONCE("[Odometry]: Publishing odometry");

    // publish TF
    geometry_msgs::Vector3 position;
    position.x = odom_main.pose.pose.position.x;
    position.y = odom_main.pose.pose.position.y;
    position.z = odom_main.pose.pose.position.z;
    geometry_msgs::TransformStamped tf;
    tf.header.stamp          = ros::Time::now();
    tf.header.frame_id       = "local_origin";
    tf.child_frame_id        = std::string("fcu_") + uav_name;
    tf.transform.translation = position;
    tf.transform.rotation    = odom_main.pose.pose.orientation;
    try {
      broadcaster_->sendTransform(tf);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing TF: %s - %s.", tf.child_frame_id.c_str(), tf.header.frame_id.c_str());
    }

    if (!isUavFlying()) {
      ROS_WARN_THROTTLE(5.0, "[Odometry]: Preflight check: \nodom: x: %f y: %f z: %f\n%s: x: %f y: %f\nlateral_estimator: %s\naltitude_estimator: %s",
                        odom_main.pose.pose.position.x, odom_main.pose.pose.position.y, odom_main.pose.pose.position.z,
                        use_local_origin_ ? "local_origin" : "utm_origin", use_local_origin_ ? local_origin_x_ : utm_origin_x_,
                        use_local_origin_ ? local_origin_y_ : utm_origin_y_, current_estimator_name.c_str(), current_alt_estimator_name.c_str());
    }
  }

  //}

  //}

  /* //{ auxTimer() */

  void Odometry::auxTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("auxTimer", aux_rate_, 0.004, event);

    ros::Time t_pub = ros::Time::now();

    // Loop through each estimator
    for (auto &estimator : m_state_estimators) {

      std::map<std::string, nav_msgs::Odometry>::iterator odom_aux = map_estimator_odom.find(estimator.first);

      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        odom_aux->second.pose = odom_pixhawk_shifted.pose;
      }

      odom_aux->second.header.frame_id = "local_origin";
      odom_aux->second.header.stamp    = t_pub;

      Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(altitude_n, 1);
      // update the altitude state
      {
        std::scoped_lock lock(mutex_altitude_estimator);
        if (!current_alt_estimator->getStates(current_altitude)) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator not initialized.");
          return;
        }

        if (_alt_estimator_type.type == mrs_msgs::AltitudeType::ALTITUDE || _alt_estimator_type.type == mrs_msgs::AltitudeType::ELEVATION) {
          odom_aux->second.pose.pose.position.z = current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);
        } else if (_alt_estimator_type.type == mrs_msgs::AltitudeType::HEIGHT) {
          odom_aux->second.pose.pose.position.z = current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT);
        } else {
          ROS_ERROR_THROTTLE(1.0, "[Odometry]: unknown altitude type: %d", _alt_estimator_type.type);
        }
      }

      if (current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT) == fcu_height_) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Suspicious height detected: %f, %f , %f, %f. Check if altitude fusion is running correctly", current_altitude(0),
                          current_altitude(1), current_altitude(2), current_altitude(3));
      }

      Eigen::VectorXd pos_vec(2);
      Eigen::VectorXd vel_vec(2);

      estimator.second->getState(0, pos_vec);
      estimator.second->getState(1, vel_vec);

      odom_aux->second.pose.pose.position.x = pos_vec(0);
      odom_aux->second.twist.twist.linear.x = vel_vec(0);
      odom_aux->second.pose.pose.position.y = pos_vec(1);
      odom_aux->second.twist.twist.linear.y = vel_vec(1);

      if (std::strcmp(estimator.second->getName().c_str(), "OBJECT") == STRING_EQUAL) {
        odom_aux->second.child_frame_id = "OBJECT" + odom_object.child_frame_id;
      }

      std::map<std::string, ros::Publisher>::iterator pub_odom_aux = map_estimator_pub.find(estimator.second->getName());

      try {
        pub_odom_aux->second.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(odom_aux->second)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odom_aux->second.getTopic().c_str());
      }
    }

    // Loop through each heading estimator
    for (auto &estimator : m_heading_estimators) {

      /* std::map<std::string, mrs_msgs::Float64ArrayStamped>::iterator heading_aux = map_hdg_estimator_msg.find(estimator.first); */

      mrs_msgs::Float64ArrayStamped heading_aux;

      heading_aux.header.frame_id = "local_origin";
      heading_aux.header.stamp    = t_pub;

      Eigen::MatrixXd current_heading = Eigen::MatrixXd::Zero(heading_n, 1);
      // update the altitude state
      {
        std::scoped_lock lock(mutex_heading_estimator);
        if (!estimator.second->getStates(current_heading)) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Heading estimator not initialized.");
          return;
        }
      }

      current_heading(0) = wrap(current_heading(0));

      for (int i = 0; i < current_heading.rows(); i++) {
        heading_aux.values.push_back(current_heading(i));
      }

      std::map<std::string, ros::Publisher>::iterator pub_hdg_aux = map_hdg_estimator_pub.find(estimator.second->getName());

      try {
        pub_hdg_aux->second.publish(mrs_msgs::Float64ArrayStampedConstPtr(new mrs_msgs::Float64ArrayStamped(heading_aux)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_hdg_aux->second.getTopic().c_str());
      }
    }
    ROS_INFO_ONCE("[Odometry]: Publishing auxiliary odometry");
  }

  //}

  /* //{ slowOdomTimer() */

  void Odometry::slowOdomTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("slowOdomTimer", slow_odom_rate_, 0.01, event);

    nav_msgs::Odometry slow_odom;

    {
      std::scoped_lock lock(mutex_shared_odometry);

      slow_odom = shared_odom;
    }

    try {
      pub_slow_odom_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(slow_odom)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_slow_odom_.getTopic().c_str());
    }

    mrs_msgs::EspOdometry esp_odom;
    esp_odom.header = slow_odom.header;
    esp_odom.posx   = slow_odom.pose.pose.position.x;
    esp_odom.posy   = slow_odom.pose.pose.position.y;
    esp_odom.posz   = slow_odom.pose.pose.position.z;
    esp_odom.velx   = slow_odom.twist.twist.linear.x;
    esp_odom.vely   = slow_odom.twist.twist.linear.y;
    esp_odom.velz   = slow_odom.twist.twist.linear.z;

    try {
      pub_esp_odom_.publish(mrs_msgs::EspOdometryConstPtr(new mrs_msgs::EspOdometry(esp_odom)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_esp_odom_.getTopic().c_str());
    }
  }

  //}

  /* //{ diagTimer() */

  void Odometry::diagTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;


    mrs_lib::Routine profiler_routine = profiler->createRoutine("diagTimer", diag_rate_, 0.01, event);

    mrs_msgs::OdometryDiag odometry_diag;

    odometry_diag.header.stamp = ros::Time::now();

    odometry_diag.estimator_type = _estimator_type;

    odometry_diag.max_altitude      = max_altitude;
    odometry_diag.gps_reliable      = gps_reliable;
    odometry_diag.gps_available     = _gps_available;
    odometry_diag.optflow_available = _optflow_available;
    odometry_diag.rtk_available     = _rtk_available;
    odometry_diag.lidar_available   = _lidar_available;


    try {
      pub_odometry_diag_.publish(odometry_diag);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odometry_diag_.getTopic().c_str());
    }
  }

  //}

  /* //{ lkfStatesTimer() */

  void Odometry::lkfStatesTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("lkfStatesTimer", lkf_states_rate_, 0.01, event);

    Eigen::MatrixXd states_mat = Eigen::MatrixXd::Zero(lateral_n, 2);
    /* Eigen::MatrixXd cov_mat; */

    // get states and covariances from lateral kalman
    {
      std::scoped_lock lock(mutex_current_estimator);

      current_estimator->getStates(states_mat);
      /* cov_mat    = current_estimator->getQ(); */
    }

    // convert eigen matrix to std::vector
    /* std::vector<double> cov_vec(cov_mat.data(), cov_mat.data() + cov_mat.rows() * cov_mat.cols()); */

    /* Eigen::EigenSolver<Eigen::MatrixXd> es(cov_mat); */

    /* // fill the message */
    mrs_msgs::LkfStates lkf_states_x;
    mrs_msgs::LkfStates lkf_states_y;
    /* for (int i = 0; i < cov_mat.rows(); ++i) { */
    /*   lkf_states.eigenvalues[i] = (es.eigenvalues().col(0)[i].real()); */
    /* } */

    /* for (int i = 0; i < cov_mat.rows(); i++) { */
    /*   lkf_states.covariance[i] = cov_mat(i, i); */
    /* } */

    lkf_states_x.header.stamp = ros::Time::now();
    lkf_states_x.pos          = states_mat(0, 0);
    lkf_states_x.vel          = states_mat(1, 0);
    lkf_states_x.acc          = states_mat(2, 0);
    lkf_states_x.acc_i        = states_mat(3, 0);
    lkf_states_x.acc_d        = states_mat(4, 0);
    lkf_states_x.tilt         = states_mat(5, 0);

    lkf_states_y.header.stamp = ros::Time::now();
    lkf_states_y.pos          = states_mat(0, 1);
    lkf_states_y.vel          = states_mat(1, 1);
    lkf_states_y.acc          = states_mat(2, 1);
    lkf_states_y.acc_i        = states_mat(3, 1);
    lkf_states_y.acc_d        = states_mat(4, 1);
    lkf_states_y.tilt         = states_mat(5, 1);

    try {
      pub_lkf_states_x_.publish(lkf_states_x);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_lkf_states_x_.getTopic().c_str());
    }

    try {
      pub_lkf_states_y_.publish(lkf_states_y);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_lkf_states_y_.getTopic().c_str());
    }
  }

  //}

  /* //{ maxAltitudeTimer() */

  void Odometry::maxAltitudeTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("maxAltitudeTimer", max_altitude_rate_, 0.01, event);

    mrs_msgs::Float64Stamped max_altitude_msg;
    max_altitude_msg.header.frame_id = "local_origin";
    max_altitude_msg.header.stamp    = ros::Time::now();

    max_altitude_msg.value = max_altitude;

    try {
      pub_max_altitude_.publish(max_altitude_msg);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_max_altitude_.getTopic().c_str());
    }
  }

  //}

  /* //{ rtkRateTimer() */

  void Odometry::rtkRateTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("rtkRateTimer", 1, 0.01, event);

    if (got_rtk) {

      if (got_rtk_counter < 15) {

        ROS_ERROR_THROTTLE(1.0, "[Odometry]: RTK comming at slow rate (%d Hz)!", got_rtk_counter);
      }

      got_rtk_counter = 0;
    }
  }

  //}

  /* //{ topicWatcherTimer() */

  void Odometry::topicWatcherTimer(const ros::TimerEvent &event) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("topicWatcherTimer", topic_watcher_rate_, 0.01, event);

    ros::Duration interval;

    // pixhawk odometry
    interval = ros::Time::now() - odom_pixhawk_last_update;
    if (got_odom_pixhawk && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: Pixhawk odometry not received for %f seconds.", interval.toSec());
      got_odom_pixhawk = false;
    }

    // optflow velocities
    interval = ros::Time::now() - optflow_twist_last_update;
    if (got_optflow && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: Optflow twist not received for %f seconds.", interval.toSec());
      got_optflow      = false;
      optflow_reliable = false;
    }

    //  target attitude
    interval = ros::Time::now() - target_attitude_last_update;
    if (got_target_attitude && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: Target attitude not received for %f seconds.", interval.toSec());
      got_target_attitude = false;
    }

    // rtk odometry
    /* interval = ros::Time::now() - rtk_last_update; */
    /* if (got_rtk && interval.toSec() > 1.0) { */
    /*   ROS_WARN("[Odometry]: RTK msg not received for %f seconds.", interval.toSec()); */
    /*   got_rtk = false; */
    /* } */

    //  vio odometry
    interval = ros::Time::now() - odom_vio_last_update;
    if (got_vio && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: VIO odometry not received for %f seconds.", interval.toSec());
      got_vio = false;
    }

    //  object odometry
    interval = ros::Time::now() - odom_object_last_update;
    if (got_object && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: OBJECT odometry not received for %f seconds.", interval.toSec());
      got_object      = false;
      object_reliable = false;
    }

    //  icp velocities
    interval = ros::Time::now() - icp_odom_last_update;
    if (got_icp && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: ICP velocities not received for %f seconds.", interval.toSec());
      got_icp = false;
    }

    //  icp position
    interval = ros::Time::now() - icp_global_odom_last_update;
    if (got_icp_global && interval.toSec() > 1.0) {
      ROS_WARN("[Odometry]: ICP position not received for %f seconds.", interval.toSec());
      got_icp_global = false;
    }
  }

  //}

  /* //{ transformTimer() */

  void Odometry::transformTimer(const ros::TimerEvent &event) {

    if (!is_initialized || !got_init_heading)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("transformTimer", 1, 0.01, event);

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, m_init_heading);
    q.normalize();

    geometry_msgs::TransformStamped tf;
    tf.header.stamp          = ros::Time::now();
    tf.header.frame_id       = "local_origin";
    tf.child_frame_id        = std::string("fcu_") + uav_name + std::string("_origin");
    tf.transform.translation = tf2::toMsg(tf2::Vector3(0.0, 0.0, 0.0));
    tf.transform.rotation    = tf2::toMsg(q);
    try {
      broadcaster_->sendTransform(tf);
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing TF: %s - %s.", tf.child_frame_id.c_str(), tf.header.frame_id.c_str());
    }
  }

  //}

  // --------------------------------------------------------------
  // |                          callbacks                         |
  // --------------------------------------------------------------

  // | ------------------ subscriber callbacks ------------------ |

  /* //{ callbackTargetAttitude() */
  void Odometry::callbackTargetAttitude(const mavros_msgs::AttitudeTargetConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackTargetAttitude");

    {
      std::scoped_lock lock(mutex_target_attitude);

      if (got_target_attitude) {

        target_attitude_previous     = target_attitude;
        target_attitude              = *msg;
        target_attitude.header.stamp = ros::Time::now();  // why?

      } else {

        target_attitude              = *msg;
        target_attitude.header.stamp = ros::Time::now();  // why?
        target_attitude_previous     = target_attitude;

        got_target_attitude = true;
        return;
      }
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    target_attitude_last_update = ros::Time::now();

    if (!isTimestampOK(target_attitude.header.stamp.toSec(), target_attitude_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Target attitude timestamp not OK, skipping prediction of lateral estimators.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    // rotations in inertial frame
    double rot_x, rot_y, rot_z;
    double dt;
    {
      std::scoped_lock lock(mutex_target_attitude);

      dt = (target_attitude.header.stamp - target_attitude_previous.header.stamp).toSec();
      getGlobalRot(target_attitude.orientation, rot_x, rot_y, rot_z);
    }
    // For model testing
    /* { getGlobalRot(ground_truth.pose.pose.orientation, rot_x, rot_y, rot_z); } */
    target_attitude_global.header   = target_attitude.header;
    target_attitude_global.vector.x = rot_x;
    target_attitude_global.vector.y = rot_y;
    target_attitude_global.vector.z = rot_z;
    pub_target_attitude_global_.publish(target_attitude_global);

    if (!std::isfinite(rot_x)) {
      rot_x = 0;
      ROS_ERROR("[Odometry]: NaN detected in target attitude variable \"rot_x\", setting it to 0!!!");
      return;
    } else if (rot_x > 1.57) {
      rot_x = 1.57;
    } else if (rot_x < -1.57) {
      rot_x = -1.57;
    }

    if (!std::isfinite(rot_y)) {
      rot_y = 0;
      ROS_ERROR("[Odometry]: NaN detected in target attitude variable \"rot_y\", setting it to 0!!!");
      return;
    } else if (rot_y > 1.57) {
      rot_y = 1.57;
    } else if (rot_y < -1.57) {
      rot_y = -1.57;
    }

    if (!std::isfinite(dt)) {
      dt = 0;
      ROS_ERROR("NaN detected in Mavros variable \"dt\", setting it to 0 and returning!!!");
      return;
    } else if (dt > 1) {
      ROS_ERROR("Mavros variable \"dt\" > 1, setting it to 1 and returning!!!");
      dt = 1;
      return;
    } else if (dt < -1) {
      ROS_ERROR("Mavros variable \"dt\" < -1, setting it to -1 and returning!!!");
      dt = -1;
      return;
    }

    double yaw_rate = -target_attitude.body_rate.z;

    if (!std::isfinite(yaw_rate)) {
      ROS_ERROR("NaN detected in Mavros variable \"yaw_rate\", prediction with zero input!!!");
      yaw_rate = 0.0;
    }

    // Apply prediction step to all heading estimators
    headingEstimatorsPrediction(rot_z, yaw_rate, dt);

    if (!got_lateral_sensors) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing target attitude. Waiting for other sensors.");
      return;
    }

    // Apply prediction step to all state estimators
    stateEstimatorsPrediction(rot_y, -rot_x, dt);

    ROS_INFO_ONCE("[Odometry]: Prediction step of all state estimators running.");
  }

  //}

  /* //{ callbackMavrosOdometry() */
  void Odometry::callbackMavrosOdometry(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackOdometry");

    if (got_odom_pixhawk) {

      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        odom_pixhawk_previous         = odom_pixhawk;
        odom_pixhawk_previous_shifted = odom_pixhawk_shifted;
        odom_pixhawk                  = *msg;
        odom_pixhawk_shifted          = *msg;

        // shift the odom_pixhawk_shifted
        if (got_pixhawk_odom_offset) {
          odom_pixhawk_shifted.pose.pose.position.x += pixhawk_odom_offset_x;
          odom_pixhawk_shifted.pose.pose.position.y += pixhawk_odom_offset_y;
          odom_pixhawk_previous_shifted.pose.pose.position.x += pixhawk_odom_offset_x;
          odom_pixhawk_previous_shifted.pose.pose.position.y += pixhawk_odom_offset_y;
          got_pixhawk_odom_shifted = true;
        }
      }

    } else {

      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        odom_pixhawk_previous = *msg;
        odom_pixhawk          = *msg;
      }

      if (simulation_) {
        mavros_glitch.x = 0.0;
        mavros_glitch.y = 0.0;
        mavros_glitch.z = 0.0;
      }

      got_odom_pixhawk         = true;
      odom_pixhawk_last_update = ros::Time::now();
      return;
    }

    odom_pixhawk_last_update = ros::Time::now();

    if (!isTimestampOK(odom_pixhawk.header.stamp.toSec(), odom_pixhawk_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Pixhawk odom timestamp not OK, not fusing correction.");
      return;
    }

    double dt;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);

      dt = (odom_pixhawk.header.stamp - odom_pixhawk_previous.header.stamp).toSec();
    }

    // Negate weird simulation position jump glitches (caused by gazebo timestamp glitches - should be handled by isTimestampOK(), this check is for redundancy)
    // (smaller jumps in GPS position are handled by safety control mechanisms)
    if (simulation_) {

      if (std::fabs(odom_pixhawk.pose.pose.position.x - odom_pixhawk_previous.pose.pose.position.x) > 100) {
        mavros_glitch.x = odom_pixhawk.pose.pose.position.x - odom_pixhawk_previous.pose.pose.position.x;
        ROS_WARN("[Odometry]: Mavros position glitch detected. Current x: %f, Previous x: %f", odom_pixhawk.pose.pose.position.x,
                 odom_pixhawk_previous.pose.pose.position.x);
      }
      if (std::fabs(odom_pixhawk.pose.pose.position.y - odom_pixhawk_previous.pose.pose.position.y) > 100) {
        mavros_glitch.y = odom_pixhawk.pose.pose.position.y - odom_pixhawk_previous.pose.pose.position.y;
        ROS_WARN("[Odometry]: Mavros position glitch detected. Current y: %f, Previous y: %f", odom_pixhawk.pose.pose.position.y,
                 odom_pixhawk_previous.pose.pose.position.y);
      }
      if (std::fabs(odom_pixhawk.pose.pose.position.z - odom_pixhawk_previous.pose.pose.position.z) > 100) {
        mavros_glitch.z = odom_pixhawk.pose.pose.position.z - odom_pixhawk_previous.pose.pose.position.z;
        ROS_WARN("[Odometry]: Mavros position glitch detected. Current x: %f, Previous x: %f", odom_pixhawk.pose.pose.position.z,
                 odom_pixhawk_previous.pose.pose.position.z);
      }

      odom_pixhawk_shifted.pose.pose.position.x -= mavros_glitch.x;
      odom_pixhawk_shifted.pose.pose.position.y -= mavros_glitch.y;
      odom_pixhawk_shifted.pose.pose.position.z -= mavros_glitch.z;
    }

    if (!got_init_heading) {

      if (_use_heading_estimator) {
        Eigen::VectorXd hdg(1);
        current_hdg_estimator->getState(0, hdg);
        m_init_heading = hdg(0);
      } else {
        // Pixhawk (compass) orientation
        tf2::Quaternion q;
        {
          std::scoped_lock lock(mutex_odom_pixhawk);

          q.setX(odom_pixhawk.pose.pose.orientation.x);
          q.setY(odom_pixhawk.pose.pose.orientation.y);
          q.setZ(odom_pixhawk.pose.pose.orientation.z);
          q.setW(odom_pixhawk.pose.pose.orientation.w);
        }
        q.normalize();

        double roll, pitch;
        tf2::Matrix3x3(q).getRPY(roll, pitch, m_init_heading);
      }
      got_init_heading = true;
    }

    if (!got_range) {

      return;
    }


    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    // set the input vector
    Eigen::VectorXd input;
    input = Eigen::VectorXd::Zero(altitude_m);


    /* altitude estimator update //{ */

    if (is_altitude_estimator_initialized) {

      /* publish covariance of altitude states //{ */

      Eigen::MatrixXd cov = Eigen::MatrixXd::Zero(altitude_n, altitude_n);
      {
        std::scoped_lock lock(mutex_altitude_estimator);
        current_alt_estimator->getCovariance(cov);
      }
      mrs_msgs::Float64ArrayStamped cov_msg;
      cov_msg.header.stamp    = ros::Time::now();
      cov_msg.header.frame_id = "local_origin";
      for (int i = 0; i < cov.rows(); i++) {
        cov_msg.values.push_back(cov(i, i));
      }
      try {
        pub_alt_cov_.publish(mrs_msgs::Float64ArrayStampedConstPtr(new mrs_msgs::Float64ArrayStamped(cov_msg)));
      }
      catch (...) {
        ROS_ERROR("Exception caught during publishing topic %s.", pub_alt_cov_.getTopic().c_str());
      }

      //}

      /* do correction of barometer altitude //{ */

      for (auto &estimator : m_altitude_estimators) {
        int n_states;
        if (!estimator.second->getN(n_states)) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator not initialized.");
          return;
        }
        Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(n_states, 1);

        if (!current_alt_estimator->getStates(current_altitude)) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator not initialized.");
          return;
        }

        double correction = msg->pose.pose.position.z;

        {
          std::scoped_lock lock(mutex_altitude_estimator);
          Eigen::VectorXd  input;
          input    = Eigen::VectorXd::Zero(altitude_m);
          input(0) = (correction - odom_pixhawk_previous.pose.pose.position.z) / dt;
          /* Eigen::MatrixXd B_new(6, 1); */
          /* B_new << dt, 0, dt, 0, 0, 0; */
          /* estimator.second->setB(B_new); */
          estimator.second->doPrediction(input, dt);
        }

        // For HEIGHT estimator do now use baro offset
        if (std::strcmp(estimator.first.c_str(), "HEIGHT") == 0) {

          // saturate the correction
          correction -= current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);

          if (!std::isfinite(correction)) {
            correction = 0;
            ROS_ERROR("[Odometry]: NaN detected in Barometer variable \"correction\", setting it to 0!!!");
          } else if (correction > max_altitude_correction_) {
            correction = max_altitude_correction_;
          } else if (correction < -max_altitude_correction_) {
            correction = -max_altitude_correction_;
          }

          correction += current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);

          {
            std::scoped_lock lock(mutex_altitude_estimator);
            /* altitudeEstimatorCorrection(correction, "alt_baro", estimator.second); */
          }
        } else {

          // saturate the correction
          correction -= current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);

          if (!std::isfinite(correction)) {
            correction = 0;
            ROS_ERROR("[Odometry]: NaN detected in Barometer variable \"correction\", setting it to 0!!!");
          } else if (correction - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) > max_altitude_correction_) {
            correction = max_altitude_correction_;
          } else if (correction - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) < -max_altitude_correction_) {
            correction = -max_altitude_correction_;
          }

          correction += current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE);

          // Compensate the offset
          double alt_baro = correction - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET);

          {
            std::scoped_lock lock(mutex_altitude_estimator);
            /* altitudeEstimatorCorrection(alt_baro, "alt_baro"); */
          }

          //}

          /* publish innovation covariance //{ */

          std::map<std::string, int>::iterator it_measurement_id = map_alt_measurement_name_id.find("bias_baro");
          Eigen::MatrixXd                      innovation_cov    = Eigen::MatrixXd::Zero(1, 1);
          {
            std::scoped_lock lock(mutex_altitude_estimator);
            current_alt_estimator->getInnovationCovariance(it_measurement_id->second, innovation_cov);
          }
          mrs_msgs::Float64Stamped inno_cov_msg;
          inno_cov_msg.header.stamp    = ros::Time::now();
          inno_cov_msg.header.frame_id = "local_origin";
          inno_cov_msg.value           = innovation_cov(0, 0);
          try {
            pub_inno_cov_bias_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(inno_cov_msg)));
          }
          catch (...) {
            ROS_ERROR("Exception caught during publishing topic %s.", pub_inno_cov_bias_.getTopic().c_str());
          }

          //}

          /* do correction of barometer offset //{ */

          // Estimate the initial baro offset before taking off
          if (!isUavFlying()) {
            double bias_baro = msg->pose.pose.position.z - fcu_height_;
            if (!std::isfinite(bias_baro)) {
              bias_baro = 0;
              ROS_ERROR("[Odometry]: NaN detected in Barometer variable \"bias_baro\", setting it to 0!!!");
              /* } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) > max_altitude_correction_) { */
              /*   bias_baro = max_altitude_correction_; */
              /* } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) < -max_altitude_correction_) { */
              /*   bias_baro = -max_altitude_correction_; */
            }
            {
              std::scoped_lock lock(mutex_altitude_estimator);
              /* altitudeEstimatorCorrection(bias_baro, "bias_baro"); */
            }
            /* ROS_WARN_THROTTLE(1.0, "[Odometry]: Estimating baro offset on the ground."); */
            /* ROS_WARN_THROTTLE(1.0, "Barometer bias correction: %f", bias_baro); */

          } else {

            double bias_baro =
                correction - (current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT) + current_altitude(mrs_msgs::AltitudeStateNames::ELEVATION));
            /* double innov_bias_baro = bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET); */

            // We want to estimate barometer offset only when the altitude is constant
            /* if (estimate_baro_offset && !bias_baro_estimation_enabled && current_altitude(mrs_msgs::AltitudeStateNames::VEL_ALT) <
             * _max_vel_baro_offset_estimation
             * && */
            /*     current_altitude(mrs_msgs::AltitudeStateNames::VEL_HEIGHT) < _max_vel_baro_offset_estimation) { */
            /* if (estimate_baro_offset && !bias_baro_estimation_enabled) { */
            /*   bias_baro_estimation_enabled = true; */
            /* } */
            /* if (bias_baro_estimation_enabled && (!estimate_baro_offset || current_altitude(mrs_msgs::AltitudeStateNames::VEL_ALT) >
             * _max_vel_baro_offset_estimation
             * || */
            /*                                      current_altitude(mrs_msgs::AltitudeStateNames::VEL_HEIGHT) > _max_vel_baro_offset_estimation)) { */
            /* if (bias_baro_estimation_enabled && !estimate_baro_offset) { */
            /*   bias_baro_estimation_enabled = false; */
            /* } */

            if (!obstacle_detected && !excessive_tilt) {
              /* if (!excessive_tilt) { */

              // When there is no obstacle under the drone, reset the elevation to zero
              if (estimate_elevation && current_altitude(mrs_msgs::AltitudeStateNames::ELEVATION) < _elevation_tolerance) {
                bias_baro = correction - current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT);
                if (!std::isfinite(bias_baro)) {
                  bias_baro = 0;
                  ROS_ERROR("[Odometry]: NaN detected in Barometer variable \"bias_baro\", setting it to 0!!!");
                } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) > max_altitude_correction_) {
                  bias_baro = current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) + max_altitude_correction_;
                } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) < -max_altitude_correction_) {
                  bias_baro = current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) - max_altitude_correction_;
                }
                {
                  std::scoped_lock lock(mutex_altitude_estimator);
                  /* altitudeEstimatorCorrection(bias_baro, "bias_baro", estimator.second); */
                }
                if (std::strcmp(estimator.first.c_str(), "ELEVATION") == 0) {
                  std::scoped_lock lock(mutex_altitude_estimator);
                  /* altitudeEstimatorCorrection(0.0, "elevation", estimator.second); */
                }

              } else {
                if (!std::isfinite(bias_baro)) {
                  bias_baro = 0;
                  ROS_ERROR("[Odometry]: NaN detected in Barometer variable \"bias_baro\", setting it to 0!!!");
                } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) > max_altitude_correction_) {
                  bias_baro = current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) + max_altitude_correction_;
                } else if (bias_baro - current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) < -max_altitude_correction_) {
                  bias_baro = current_altitude(mrs_msgs::AltitudeStateNames::BARO_OFFSET) - max_altitude_correction_;
                }
                std::scoped_lock lock(mutex_altitude_estimator);
                /* altitudeEstimatorCorrection(bias_baro, "bias_baro", estimator.second); */
              }
              /* ROS_WARN_THROTTLE(1.0, "Barometer bias correction: %f", bias_baro); */
            }
          }
        }
      }


      ROS_WARN_ONCE("[Odometry]: fusing barometer altitude");


      //}

      /* do correction of mavros z velocity //{ */

      // create a correction value
      double correction = msg->twist.twist.linear.z;

      // saturate the correction
      if (!std::isfinite(correction)) {
        correction = 0;
        ROS_ERROR("[Odometry]: NaN detected in Mavros z velocity variable \"correction\", setting it to 0!!!");
      }

      // set the measurement vector
      double vel_mavros;
      vel_mavros = correction;

      {
        std::scoped_lock lock(mutex_altitude_estimator);

        /* altitudeEstimatorCorrection(vel_mavros, "vel_mavros"); */
        if (fabs(vel_mavros > 10)) {
          ROS_WARN("Mavros z velocity correction: %f", vel_mavros);
        }
      }

      ROS_WARN_ONCE("[Odometry]: fusing mavros z velocity");

      //}

      /*  Prediction with no control input//{ */

      /* { */
      /*   std::scoped_lock lock(mutex_altitude_estimator); */
      /*   current_alt_estimator->doPrediction(input, dt); */
      /* } */

      //}

    } else {
      Eigen::VectorXd altitude    = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd d_altitude  = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd dd_altitude = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd height      = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd elevation   = Eigen::VectorXd::Zero(1);
      Eigen::VectorXd bias        = Eigen::VectorXd::Zero(1);
      Eigen::MatrixXd init_cov    = Eigen::MatrixXd::Identity(altitude_n, altitude_n);
      init_cov *= 1000;
      d_altitude << 0.0;
      dd_altitude << 0.0;
      height << fcu_height_;
      bias << msg->pose.pose.position.z - height(0, 0);
      altitude << msg->pose.pose.position.z - bias(0);
      elevation << 0.0;

      // Initialize all altitude estimators
      for (auto &estimator : m_altitude_estimators) {
        estimator.second->setState(0, altitude);
        estimator.second->setState(1, d_altitude);
        estimator.second->setState(2, height);
        estimator.second->setState(3, d_altitude);
        estimator.second->setState(4, elevation);
        estimator.second->setState(5, bias);
        estimator.second->setCovariance(init_cov);
      }
      is_altitude_estimator_initialized = true;
    }


    //}

    /* state estimators update //{ */

    if (!got_pixhawk_odom_offset) {
      return;
    }

    /* mavros tilts in inertial frame //{ */

    double rot_x, rot_y, rot_z;
    {
      std::scoped_lock lock(mutex_odom_pixhawk_shifted);

      getGlobalRot(odom_pixhawk_shifted.pose.pose.orientation, rot_x, rot_y, rot_z);

      // publish orientation for debugging
      orientation_mavros.header = odom_pixhawk_shifted.header;
    }
    orientation_mavros.vector.x = rot_x;
    orientation_mavros.vector.y = rot_y;
    orientation_mavros.vector.z = rot_z;
    try {
      pub_orientation_mavros_.publish(orientation_mavros);
    }
    catch (...) {
      ROS_ERROR("Exception caught during publishing topic %s.", pub_orientation_mavros_.getTopic().c_str());
    }

    //}

    if (!got_lateral_sensors) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing mavros odom. Waiting for other sensors.");
      return;
    }

    /* //{ fuse mavros tilts */

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(rot_y, -rot_x, "tilt_mavros");

    ROS_WARN_ONCE("[Odometry]: Fusing mavros tilts");

    //}

    /* //{ fuse mavros velocity */

    if (_gps_available) {

      double vel_mavros_x, vel_mavros_y;
      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        // TODO test which one is better
        vel_mavros_x = (odom_pixhawk.pose.pose.position.x - odom_pixhawk_previous.pose.pose.position.x) / dt;
        /* vel_mavros_x = odom_pixhawk.twist.twist.linear.x; */
        vel_mavros_y = (odom_pixhawk.pose.pose.position.y - odom_pixhawk_previous.pose.pose.position.y) / dt;
        /* vel_mavros_y = odom_pixhawk.twist.twist.linear.y; */
      }

      // Apply correction step to all state estimators
      if (simulation_ && fabs(vel_mavros_x) < 100 && fabs(vel_mavros_y) < 100) {
        stateEstimatorsCorrection(vel_mavros_x, vel_mavros_y, "vel_mavros");
        ROS_WARN_ONCE("[Odometry]: Fusing mavros velocity");
      }

      Eigen::VectorXd rtk_input(2);
      rtk_input << vel_mavros_x, vel_mavros_y;

      {
        std::scoped_lock lock(mutex_rtk_est);

        estimator_rtk->setInput(rtk_input);
        Eigen::MatrixXd B_new(2, 2);
        B_new << dt, 0, 0, dt;
        estimator_rtk->setB(B_new);
        estimator_rtk->iterateWithoutCorrection();
      }
    }

    //}

    /* //{ fuse mavros position */

    if (_gps_available) {

      if (!got_odom_pixhawk || (use_utm_origin_ && !got_pixhawk_utm)) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing Mavros position. Global position not averaged.");
        return;
      }

      double pos_mavros_x, pos_mavros_y;

      {
        std::scoped_lock lock(mutex_odom_pixhawk_shifted);

        pos_mavros_x = odom_pixhawk_shifted.pose.pose.position.x;
        pos_mavros_y = odom_pixhawk_shifted.pose.pose.position.y;
      }

      // Saturate correction

      if (saturate_mavros_position_) {
        for (auto &estimator : m_state_estimators) {
          if (std::strcmp(estimator.first.c_str(), "GPS") == 0) {
            Eigen::VectorXd pos_vec(2);
            estimator.second->getState(0, pos_vec);

            // X position
            if (!std::isfinite(pos_mavros_x)) {
              pos_mavros_x = 0;
              ROS_ERROR("NaN detected in variable \"pos_mavros_x\", setting it to 0 and returning!!!");
              return;
            } else if (pos_mavros_x - pos_vec(0) > max_mavros_pos_correction) {
              ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating GPS X pos correction %f -> %f", pos_mavros_x - pos_vec(0), max_mavros_pos_correction);
              pos_mavros_x = pos_vec(0) + max_mavros_pos_correction;
            } else if (pos_mavros_x - pos_vec(0) < -max_mavros_pos_correction) {
              ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating GPS X pos correction %f -> %f", pos_mavros_x - pos_vec(0), -max_mavros_pos_correction);
              pos_mavros_x = pos_vec(0) - max_mavros_pos_correction;
            }

            // Y position
            if (!std::isfinite(pos_mavros_y)) {
              pos_mavros_y = 0;
              ROS_ERROR("NaN detected in variable \"pos_mavros_y\", setting it to 0 and returning!!!");
              return;
            } else if (pos_mavros_y - pos_vec(1) > max_mavros_pos_correction) {
              ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating GPS Y pos correction %f -> %f", pos_mavros_y - pos_vec(1), max_mavros_pos_correction);
              pos_mavros_y = pos_vec(1) + max_mavros_pos_correction;
            } else if (pos_mavros_y - pos_vec(1) < -max_mavros_pos_correction) {
              ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating GPS Y pos correction %f -> %f", pos_mavros_y - pos_vec(1), -max_mavros_pos_correction);
              pos_mavros_y = pos_vec(1) - max_mavros_pos_correction;
            }
          }
        }
      }

      // Apply correction step to all state estimators
      stateEstimatorsCorrection(pos_mavros_x, pos_mavros_y, "pos_mavros");
      /* ROS_INFO("[Odometry]: Fusing mavros x pos: %f", pos_mavros_x); */

      ROS_WARN_ONCE("[Odometry]: Fusing mavros position");
      //}

      //}
    }
  }

  //}

  /* //{ callbackPixhawkImu() */

  void Odometry::callbackPixhawkImu(const sensor_msgs::ImuConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackPixhawkImu");

    pixhawk_imu_last_update = ros::Time::now();

    /* double dt; */

    {
      std::scoped_lock lock(mutex_pixhawk_imu);

      if (got_pixhawk_imu) {

        pixhawk_imu_previous = pixhawk_imu;
        pixhawk_imu          = *msg;

      } else {

        pixhawk_imu_previous = *msg;
        pixhawk_imu          = *msg;
        got_pixhawk_imu      = true;

        return;
      }

      /* dt = (pixhawk_imu.header.stamp - pixhawk_imu_previous.header.stamp).toSec(); */
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(pixhawk_imu.header.stamp.toSec(), pixhawk_imu_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Pixhawk IMU timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Heading Kalman ////////////////////

    double yaw_rate;
    {
      std::scoped_lock lock(mutex_pixhawk_imu);
      yaw_rate = pixhawk_imu.angular_velocity.z;
    }

    if (std::isfinite(yaw_rate)) {
      // Apply correction step to all heading estimators
      headingEstimatorsCorrection(yaw_rate, "rate_gyro");

      ROS_WARN_ONCE("[Odometry]: Fusing gyro yaw rate from PixHawk IMU");

    } else {

      ROS_ERROR("NaN detected in PixHawk IMU variable \"yaw_rate\", not fusing!!!");
    }
  }

  //}

  /* //{ callbackPixhawkCompassHdg() */

  void Odometry::callbackPixhawkCompassHdg(const std_msgs::Float64ConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackPixhawkCompassHdg");

    if (!init_hdg_avg_done) {
      ROS_INFO_ONCE("[Odometry]: Averaging initial compass heading.");
      double yaw;
      {
        std::scoped_lock lock(mutex_compass_hdg);
        yaw = compass_hdg.data;
      }

      yaw = yaw / 180 * M_PI;
      yaw = yaw / 180 * M_PI;
      yaw = unwrap(yaw, yaw_previous);

      init_hdg_avg += M_PI / 2 - yaw;
      if (++init_hdg_avg_samples > 100) {
        init_hdg_avg /= 100;
        init_hdg_avg_done = true;
        ROS_INFO("[Odometry]: Initial compass heading averaged to %f", init_hdg_avg);
      }
      return;
    }


    {
      std::scoped_lock lock(mutex_compass_hdg);

      if (got_compass_hdg) {

        compass_hdg_previous = compass_hdg;
        compass_hdg          = *msg;

      } else {

        compass_hdg_previous    = *msg;
        compass_hdg             = *msg;
        yaw_previous            = msg->data / 180 * M_PI;
        got_compass_hdg         = true;
        compass_hdg_last_update = ros::Time::now();

        return;
      }
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    // Compass heading msg does not have timestamp - check at least time of msg arrival
    if (!isTimestampOK(ros::Time::now().toSec(), compass_hdg_last_update.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Pixhawk compass heading time between msgs not OK, not fusing correction.");
      compass_hdg_last_update = ros::Time::now();
      return;
    }
    compass_hdg_last_update = ros::Time::now();
    //////////////////// Fuse Heading Kalman ////////////////////

    double yaw;
    {
      std::scoped_lock lock(mutex_compass_hdg);
      yaw = compass_hdg.data;
    }

    yaw          = yaw / 180 * M_PI;
    yaw          = unwrap(yaw, yaw_previous);
    yaw_previous = yaw;
    yaw          = M_PI / 2 - yaw;

    if (!compass_yaw_filter->isValid(yaw) && compass_yaw_filter->isFilled()) {
      compass_inconsistent_samples++;
      ROS_WARN("[Odometry]: Compass yaw inconsistent: %f. Not fusing.", yaw);

      if (_gyro_fallback && compass_inconsistent_samples > 20) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Compass inconsistent. Swtiching to GYRO heading estimator.");
        mrs_msgs::HeadingType desired_estimator;
        desired_estimator.type = mrs_msgs::HeadingType::GYRO;
        desired_estimator.name = _heading_estimators_names[desired_estimator.type];
        changeCurrentHeadingEstimator(desired_estimator);
        compass_inconsistent_samples = 0;
      }
      return;
    }

    if (std::isfinite(yaw)) {

      compass_inconsistent_samples = 0;

      // Apply correction step to all heading estimators
      headingEstimatorsCorrection(yaw, "yaw_compass");

      yaw = wrap(yaw);

      mrs_msgs::Float64Stamped compass_yaw_out;
      compass_yaw_out.header.stamp    = ros::Time::now();
      compass_yaw_out.header.frame_id = "local_origin";
      compass_yaw_out.value           = yaw;
      pub_compass_yaw_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(compass_yaw_out)));

      ROS_WARN_ONCE("[Odometry]: Fusing yaw from PixHawk compass");

    } else {

      ROS_ERROR("NaN detected in PixHawk compass variable \"yaw\", not fusing!!!");
    }
  }

  //}

  /* //{ callbackOptflowTwist() */

  void Odometry::callbackOptflowTwist(const geometry_msgs::TwistWithCovarianceStampedConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackOptflowTwist");

    optflow_twist_last_update = ros::Time::now();

    {
      std::scoped_lock lock(mutex_optflow);

      if (got_optflow) {

        optflow_twist_previous = optflow_twist;
        optflow_twist          = *msg;

      } else {

        optflow_twist_previous = *msg;
        optflow_twist          = *msg;

        got_optflow      = true;
        optflow_reliable = true;

        return;
      }
    }

    if (!got_range) {
      return;
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(optflow_twist.header.stamp.toSec(), optflow_twist_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Optflow twist timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    if (!got_lateral_sensors) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing optflow velocity. Waiting for other sensors.");
      return;
    }

    static int    measurement_id     = 0;
    static bool   got_init_optflow_Q = false;
    static double init_Q             = 0.0;

    for (auto &estimator : m_state_estimators) {
      if (std::strcmp(estimator.first.c_str(), "OPTFLOW") == 0) {

        // Get initial Q
        if (!got_init_optflow_Q) {
          std::string                          measurement_name  = "vel_optflow";
          std::map<std::string, int>::iterator it_measurement_id = map_measurement_name_id.find(measurement_name);
          if (it_measurement_id == map_measurement_name_id.end()) {
            ROS_ERROR("[Odometry]: Tried to set covariance of measurement with invalid name: \'%s\'.", measurement_name.c_str());
            return;
          }

          measurement_id = it_measurement_id->second;

          estimator.second->getQ(init_Q, measurement_id);
          got_init_optflow_Q = true;
        }
      }
    }

    if (_dynamic_optflow_cov) {
      double twist_q_x = optflow_twist.twist.covariance[0];
      double twist_q_y = optflow_twist.twist.covariance[7];

      if (std::isfinite(twist_q_x)) {

        // Scale covariance
        twist_q_x *= _dynamic_optflow_cov_scale;
        twist_q_y *= _dynamic_optflow_cov_scale;

        double twist_q = std::max(twist_q_x, twist_q_y);

        std::string                          measurement_name  = "vel_optflow";
        std::map<std::string, int>::iterator it_measurement_id = map_measurement_name_id.find(measurement_name);
        if (it_measurement_id == map_measurement_name_id.end()) {
          ROS_ERROR("[Odometry]: Tried to set covariance of measurement with invalid name: \'%s\'.", measurement_name.c_str());
          return;
        }

        for (auto &estimator : m_state_estimators) {
          if (std::strcmp(estimator.first.c_str(), "OPTFLOW") == 0 || std::strcmp(estimator.first.c_str(), "OPTFLOWGPS") == 0) {
            estimator.second->setQ(twist_q, it_measurement_id->second);
            ROS_INFO_THROTTLE(5.0, "[Odometry]: estimator: %s setting Q_optflow_twist to: %f", estimator.first.c_str(), twist_q);
          }
        }
      } else {
        twist_q_x = twist_q_x_prev;
        twist_q_y = twist_q_y_prev;
      }
      twist_q_x_prev = twist_q_x;
      twist_q_y_prev = twist_q_y;
    }

    double optflow_vel_x, optflow_vel_y;
    {
      std::scoped_lock lock(mutex_optflow);

      optflow_vel_x = optflow_twist.twist.twist.linear.x;
      optflow_vel_y = optflow_twist.twist.twist.linear.y;
    }

    if (_optflow_median_filter) {
      if (!optflow_filter_x->isValid(optflow_vel_x)) {

        double median = optflow_filter_x->getMedian();
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Optic flow x velocity filtered by median filter. %f -> %f", optflow_vel_x, median);
        optflow_vel_x = median;
      }

      if (!optflow_filter_y->isValid(optflow_vel_y)) {
        double median = optflow_filter_y->getMedian();
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Optic flow y velocity filtered by median filter. %f -> %f", optflow_vel_y, median);
        optflow_vel_y = median;
      }
    }
    geometry_msgs::TwistWithCovarianceStamped optflow_filtered = optflow_twist;
    optflow_filtered.twist.twist.linear.x                      = optflow_vel_x;
    optflow_filtered.twist.twist.linear.y                      = optflow_vel_y;

    try {
      pub_debug_optflow_filter.publish(geometry_msgs::TwistWithCovarianceStampedConstPtr(new geometry_msgs::TwistWithCovarianceStamped(optflow_filtered)));
    }
    catch (...) {
      ROS_ERROR("Exception caught during publishing topic %s.", pub_debug_optflow_filter.getTopic().c_str());
    }

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(optflow_vel_x, optflow_vel_y, "vel_optflow");

    ROS_WARN_ONCE("[Odometry]: Fusing optflow velocity");

    double yaw_rate;
    {
      std::scoped_lock lock(mutex_optflow);
      yaw_rate = optflow_twist.twist.twist.angular.z;
    }

    if (!optflow_yaw_rate_filter->isValid(yaw_rate)) {

      ROS_WARN_THROTTLE(1.0, "[Odometry]: Yaw rate from optic flow is inconsistent. Not fusing.");
      return;
    }

    if (!optflow_yaw_rate_filter->isValid(yaw_rate) && optflow_yaw_rate_filter->isFilled()) {
      optflow_inconsistent_samples++;
      ROS_WARN("[Odometry]: Optflow yaw rate inconsistent: %f. Not fusing.", yaw_rate);

      if (_gyro_fallback && optflow_inconsistent_samples > 20) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Optflow yaw rate inconsistent. Swtiching to GYRO heading estimator.");
        mrs_msgs::HeadingType desired_estimator;
        desired_estimator.type = mrs_msgs::HeadingType::GYRO;
        desired_estimator.name = _heading_estimators_names[desired_estimator.type];
        changeCurrentHeadingEstimator(desired_estimator);
        --optflow_inconsistent_samples;
        optflow_inconsistent_samples = std::max(0, optflow_inconsistent_samples);
      }
      return;
    }


    if (std::isfinite(yaw_rate)) {

      // Apply correction step to all heading estimators
      headingEstimatorsCorrection(yaw_rate, "rate_optflow");

      ROS_WARN_ONCE("[Odometry]: Fusing optflow yaw rate");

    } else {

      ROS_ERROR("NaN detected in optflow variable \"yaw_rate\", not fusing!!!");
    }
  }

  //}

  /* //{ callbackRtkGps() */

  void Odometry::callbackRtkGps(const mrs_msgs::RtkGpsConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackRtk");

    mrs_msgs::RtkGps rtk_utm;

    if (msg->header.frame_id.compare("gps") == STRING_EQUAL) {

      if (!std::isfinite(msg->gps.latitude)) {
        ROS_ERROR_THROTTLE(1.0, "[Odometry] NaN detected in RTK variable \"msg->latitude\"!!!");
        return;
      }

      if (!std::isfinite(msg->gps.longitude)) {
        ROS_ERROR_THROTTLE(1.0, "[Odometry] NaN detected in RTK variable \"msg->longitude\"!!!");
        return;
      }

      // convert it to UTM
      mrs_lib::UTM(msg->gps.latitude, msg->gps.longitude, &rtk_utm.pose.pose.position.x, &rtk_utm.pose.pose.position.y);
      rtk_utm.header               = msg->header;
      rtk_utm.header.frame_id      = "utm";
      rtk_utm.pose.pose.position.z = msg->gps.altitude;
      // | ----------------------- #TODO fixme ---------------------- |
      /* rtk_utm.pose.covariance      = msg->gps.covariance; */
      rtk_utm.fix_type = msg->fix_type;
      rtk_utm.status   = msg->status;

    } else if (msg->header.frame_id.compare("utm") == STRING_EQUAL) {

      rtk_utm = *msg;

    } else {

      ROS_INFO_THROTTLE(1.0, "[Odometry]: RTK message has unknown frame_id: '%s'", msg->header.frame_id.c_str());
    }

    {
      std::scoped_lock lock(mutex_rtk);

      rtk_local_previous = rtk_local;
      rtk_local          = rtk_utm;

      if (++got_rtk_counter > 2) {

        got_rtk         = true;
        rtk_last_update = ros::Time::now();
      }
    }

    if (!got_odom_pixhawk || !got_rtk) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not received RTK yet.");
      return;
    }

    if (!isTimestampOK(rtk_local.header.stamp.toSec(), rtk_local_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: RTK local timestamp not OK, not fusing correction.");
      return;
    }

    // check whether we have rtk fix
    got_rtk_fix =
        (rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::RTK_FLOAT || rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::RTK_FIX) ? true : false;

    if (_rtk_fuse_sps) {
      if (rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::NO_FIX || rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::UNKNOWN) {
        ROS_WARN_THROTTLE(1.0, "RTK fix type: NO_FIX. Not fusing RTK.");
        return;
      }
    } else {
      if (!got_rtk_fix) {
        ROS_WARN_THROTTLE(1.0, "RTK not fusing SPS.");
        return;
      }
    }

    double dt;

    {
      std::scoped_lock lock(mutex_rtk);

      dt = (rtk_local.header.stamp - rtk_local_previous.header.stamp).toSec();
    }

    // | ------------- offset the rtk to local_origin ------------- |
    rtk_local.pose.pose.position.x -= utm_origin_x_;
    rtk_local.pose.pose.position.y -= utm_origin_y_;

    rtk_local.header.frame_id = "local_origin";

    // | ------------------ publish the rtk local ----------------- |
    mrs_msgs::RtkGps rtk_local_out = rtk_local;

    try {
      pub_rtk_local.publish(mrs_msgs::RtkGpsConstPtr(new mrs_msgs::RtkGps(rtk_local_out)));
    }
    catch (...) {
      ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_rtk_local.getTopic().c_str());
    }
    // | ------------- publish the rtk local odometry ------------- |
    {
      std::scoped_lock lock(mutex_rtk_local_odom);

      rtk_local_odom.header = rtk_local.header;
      rtk_local_odom.pose   = rtk_local.pose;
      rtk_local_odom.twist  = rtk_local.twist;

      try {
        pub_rtk_local_odom.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(rtk_local_odom)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_rtk_local_odom.getTopic().c_str());
      }
    }

    // | ----------------------------- --------------------------- |


    /* if (rtk_reliable && (rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::NO_FIX || rtk_local.fix_type.fix_type == mrs_msgs::RtkFixType::UNKNOWN)) { */
    /* if (rtk_reliable && !got_rtk_fix) { */
    /*   rtk_reliable = false; */
    /*   ROS_WARN("[Odometry]: RTK unreliable."); */
    /* } else if (got_rtk_fix) { */
    /*   if (!rtk_reliable) { */
    /*   ROS_WARN("[Odometry]: RTK reliable."); */
    /*   } */
    /*   rtk_reliable = true; */
    /* } */

    // continue to lateral and altitude fusion only when we got a fix
    /* if (!got_rtk_fix) { */

    /*   return; */
    /* } */

    if (_rtk_available) {
      if (!std::isfinite(rtk_local.pose.pose.position.x) || !std::isfinite(rtk_local.pose.pose.position.y)) {

        ROS_ERROR_THROTTLE(1, "[Odometry]: NaN detected in variable \"rtk_local.pose.pose.position.x\" or \"rtk_local.pose.pose.position.y\" (rtk)!!!");

        return;
      }

      if (!got_lateral_sensors) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing RTK position. Waiting for other sensors.");
        return;
      }

      double x_rtk, y_rtk;
      {
        std::scoped_lock lock(mutex_rtk);

        x_rtk = rtk_local.pose.pose.position.x;
        y_rtk = rtk_local.pose.pose.position.y;
      }

      if (!std::isfinite(x_rtk)) {
        ROS_ERROR("NaN detected in variable \"x_rtk\" (callbackRtk)!!!");
        return;
      }

      if (!std::isfinite(y_rtk)) {
        ROS_ERROR("NaN detected in variable \"y_rtk\" (callbackRtk)!!!");
        return;
      }

      /* if (!got_rtk_fix && (fabs(rtk_local.pose.pose.position.x-rtk_local_previous.pose.pose.position.x) > 0.5 ||
       * fabs(rtk_local.pose.pose.position.y-rtk_local_previous.pose.pose.position.y) > 0.5)) { */
      /*  rtk_jump_detected = true; */
      /* } */

      /* if (got_rtk_fix) { */
      /*   rtk_jump_detected = false; */
      /* } */

      /* if (!rtk_odom_initialized) { */

      /*   Eigen::VectorXd state(2); */
      /*   Eigen::VectorXd state_vel(2); */
      /*   state << x_rtk, y_rtk; */
      /*   state_vel << 0, 0; */
      /*   /1* current_estimator->setState(0, state); *1/ */
      /*   for (auto &estimator : m_state_estimators) { */
      /*   if (std::strcmp(estimator.second->getName().c_str(), "RTK") == STRING_EQUAL) { */
      /*     estimator.second->setState(0, state); */
      /*     estimator.second->setState(1, state_vel); */
      /*   } */
      /*   } */
      /*   rtk_odom_initialized = true; */
      /* } */

      /* //{ fuse rtk velocity */

      double vel_rtk_x, vel_rtk_y;
      {
        std::scoped_lock lock(mutex_rtk);

        vel_rtk_x = (rtk_local.pose.pose.position.x - rtk_local_previous.pose.pose.position.x) / dt;
        vel_rtk_y = (rtk_local.pose.pose.position.y - rtk_local_previous.pose.pose.position.y) / dt;
      }

      // Apply correction step to all state estimators
      if (simulation_ && fabs(vel_rtk_x) < 100 && fabs(vel_rtk_y) < 100) {
        /* stateEstimatorsCorrection(vel_rtk_x, vel_rtk_y, "vel_rtk"); */
      }

      /* ROS_WARN_ONCE("[Odometry]: Fusing RTK velocity"); */

      //}

      /* fuse rtk position //{ */
      // Saturate correction
      double x_correction;
      double y_correction;
      /* Eigen::VectorXd pos_vec(2); */
      /* Eigen::VectorXd vel_vec(2); */
      /* for (auto &estimator : m_state_estimators) { */
      /* if  (std::strcmp(estimator.first.c_str(), "RTK") == 0) { */
      /* estimator.second->getState(0, pos_vec); */
      /* estimator.second->getState(1, vel_vec); */
      double x_est;
      double y_est;
      {
        std::scoped_lock lock(mutex_rtk_est);

        x_est = estimator_rtk->getState(0);
        y_est = estimator_rtk->getState(1);
      }

      // X position
      x_correction = x_rtk - x_est;
      if (!std::isfinite(x_rtk)) {
        x_rtk = 0;
        ROS_ERROR("NaN detected in variable \"x_rtk\", setting it to 0 and returning!!!");
        return;
      }
      if (x_correction > max_rtk_pos_correction) {
        x_correction = max_rtk_pos_correction;
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating RTK X pos correction %f -> %f", x_correction, max_rtk_pos_correction);
      } else if (x_correction < -max_rtk_pos_correction) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating RTK X pos correction %f -> %f", x_correction, -max_rtk_pos_correction);
        x_correction = -max_rtk_pos_correction;
      }

      // Y position
      y_correction = y_rtk - y_est;
      if (!std::isfinite(y_rtk)) {
        y_rtk = 0;
        ROS_ERROR("NaN detected in variable \"y_rtk\", setting it to 0 and returning!!!");
        return;
      }
      if (y_correction > max_rtk_pos_correction) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating RTK Y pos correction %f -> %f", y_correction, max_rtk_pos_correction);
        y_correction = max_rtk_pos_correction;
      } else if (y_correction < -max_rtk_pos_correction) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating RTK Y pos correction %f -> %f", y_correction, -max_rtk_pos_correction);
        y_correction = -max_rtk_pos_correction;
      }
      /* } */
      /* } */


      ROS_INFO_THROTTLE(1.0, "[Odometry]: pos x: %f, corr x: %f, pos y: %f, corr y: %f", x_est, x_correction, y_est, y_correction);
      // Apply correction step to all state estimators
      /* stateEstimatorsCorrection(pos_vec(0)+x_correction, pos_vec(1)+y_correction, "pos_rtk"); */

      Eigen::VectorXd rtk_meas(2);
      rtk_meas << x_est + x_correction, y_est + y_correction;
      {
        std::scoped_lock lock(mutex_rtk_est);

        estimator_rtk->setMeasurement(rtk_meas, Q_lat_rtk);
        estimator_rtk->doCorrection();
      }
      //}

      ROS_WARN_ONCE("[Odometry]: Fusing RTK position");
    }

    /* TODO RTK altitude not supported now //{ */

    /* if (rtk_altitude_enabled) { */

    /*   if (!got_rtk_fix) { */

    /*     rtk_altitude_enabled = false; */
    /*     teraranger_enabled   = true; */
    /*     garmin_enabled       = true; */
    /*     ROS_WARN("[Odometry]: We lost RTK fix, switching back to fusing teraranger and garmin."); */

    /*     return; */
    /*   } */

    /*   // ALTITUDE KALMAN FILTER */
    /*   // deside on measurement's covariance */
    /*   Eigen::MatrixXd mesCov; */
    /*   mesCov = Eigen::MatrixXd::Zero(altitude_p, altitude_p); */

    /*   if (!std::isfinite(rtk_local.pose.pose.position.z)) { */

    /*     ROS_ERROR_THROTTLE(1, "[Odometry]: NaN detected in RTK variable \"rtk_local.position.position.z\" (rtk_altitude)!!!"); */

    /*     return; */
    /*   } */

    /*   //////////////////// update rtk integral //////////////////// */
    /*   // compute the difference */
    /*   double difference = rtk_local.pose.pose.position.z - rtk_local_previous.pose.pose.position.z; */

    /*   rtk_altitude_integral += difference; */

    /*   //////////////////// Compare integral against failsafe kalman //////////////////// */
    /*   if (failsafe_teraranger_kalman->getState(0) < 5) {  // only when near to the ground */

    /*     // if rtk integral is too above failsafe kalman, switch to fusing teraranger */
    /*     if ((rtk_altitude_integral - failsafe_teraranger_kalman->getState(0)) > rtk_max_down_difference_) { */

    /*       rtk_altitude_enabled = false; */
    /*       teraranger_enabled   = true; */
    /*       ROS_ERROR("[Odometry]: RTK kalman is above failsafe kalman by more than %2.2f m!", rtk_max_down_difference_); */
    /*       ROS_ERROR("[Odometry]: Switching back to fusing teraranger!"); */

    /*       return; */
    /*     } */
    /*   } */

    /*   // if rtk integral is too above failsafe kalman, switch to fusing teraranger */
    /*   if (fabs(failsafe_teraranger_kalman->getState(0) - rtk_altitude_integral) > rtk_max_abs_difference_) { */

    /*     rtk_altitude_enabled = false; */
    /*     teraranger_enabled   = true; */
    /*     ROS_ERROR("[Odometry]: RTK kalman differs from Failsafe kalman by more than %2.2f m!", rtk_max_abs_difference_); */
    /*     ROS_ERROR("[Odometry]: Switching back to fusing teraranger!"); */

    /*     return; */
    /*   } */

    /*   // set the measurement vector */
    /*   Eigen::VectorXd mes(1); */
    /*   mes << rtk_altitude_integral; */

    /*   // set variance of gps measurement */
    /*   mesCov << rtkQ; */

    /*   { */
    /*     std::scoped_lock lock(mutex_altitude_estimator); */

    /*     main_altitude_kalman->setMeasurement(mes, mesCov); */
    /*     main_altitude_kalman->doCorrection(); */
    /*   } */

    /*   ROS_WARN_ONCE("[Odometry]: Fusing rtk altitude"); */
    /* } */

    //}
  }

  //}

  /* //{ callbackVioOdometry() */

  void Odometry::callbackVioOdometry(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackVioOdometry");

    double dt;

    if (got_vio) {

      {
        std::scoped_lock lock(mutex_odom_vio);

        odom_vio_previous = odom_vio;
        odom_vio          = *msg;
        dt                = (odom_vio.header.stamp - odom_vio_previous.header.stamp).toSec();
      }

    } else {

      {
        std::scoped_lock lock(mutex_odom_vio);

        odom_vio_previous = *msg;
        odom_vio          = *msg;
      }

      got_vio              = true;
      odom_vio_last_update = ros::Time::now();
      return;
    }

    odom_vio_last_update = ros::Time::now();

    if (!got_range) {

      return;
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(odom_vio.header.stamp.toSec(), odom_vio_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: VIO timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    /* //{ fuse vio velocity */

    double vel_vio_x, vel_vio_y;

    {
      std::scoped_lock lock(mutex_odom_vio);

      // TODO find out which one is better
      /* vel_vio_x = odom_vio.twist.twist.linear.x; */
      /* vel_vio_y = odom_vio.twist.twist.linear.y; */
      vel_vio_x = (odom_vio.pose.pose.position.x - odom_vio_previous.pose.pose.position.x) / dt;
      vel_vio_y = (odom_vio.pose.pose.position.y - odom_vio_previous.pose.pose.position.y) / dt;
    }

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(vel_vio_x, vel_vio_y, "vel_vio");

    ROS_WARN_ONCE("[Odometry]: Fusing VIO velocity");
    //}

    /* //{ fuse vio position */

    // Pixhawk (compass) orientation
    tf2::Quaternion q;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);

      q.setX(odom_pixhawk.pose.pose.orientation.x);
      q.setY(odom_pixhawk.pose.pose.orientation.y);
      q.setZ(odom_pixhawk.pose.pose.orientation.z);
      q.setW(odom_pixhawk.pose.pose.orientation.w);
    }
    q.normalize();

    double roll, pitch, yaw;
    tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);

    double vio_pos_x, vio_pos_y;
    {
      std::scoped_lock lock(mutex_odom_vio);

      // Vio orientation
      tf2::Quaternion q_vio;
      q_vio.setX(odom_vio.pose.pose.orientation.x);
      q_vio.setY(odom_vio.pose.pose.orientation.y);
      q_vio.setZ(odom_vio.pose.pose.orientation.z);
      q_vio.setW(odom_vio.pose.pose.orientation.w);
      q_vio.normalize();

      double roll_vio, pitch_vio, yaw_vio;
      tf2::Matrix3x3(q_vio).getRPY(roll_vio, pitch_vio, yaw_vio);

      // Correct the position by the compass heading
      vio_pos_x = odom_vio.pose.pose.position.x * cos(yaw - yaw_vio) - odom_vio.pose.pose.position.y * sin(yaw - yaw_vio);
      vio_pos_y = odom_vio.pose.pose.position.x * sin(yaw - yaw_vio) + odom_vio.pose.pose.position.y * cos(yaw - yaw_vio);
    }

    // Saturate correction
    for (auto &estimator : m_state_estimators) {
      if (std::strcmp(estimator.first.c_str(), "VIO") == 0) {
        Eigen::VectorXd pos_vec(2);
        estimator.second->getState(0, pos_vec);

        // X position
        if (!std::isfinite(vio_pos_x)) {
          vio_pos_x = 0;
          ROS_ERROR("NaN detected in variable \"vio_pos_x\", setting it to 0 and returning!!!");
          return;
        } else if (vio_pos_x - pos_vec(0) > max_vio_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating VIO X pos correction %f -> %f", vio_pos_x - pos_vec(0), max_vio_pos_correction);
          vio_pos_x = pos_vec(0) + max_vio_pos_correction;
        } else if (vio_pos_x - pos_vec(0) < -max_vio_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating VIO X pos correction %f -> %f", vio_pos_x - pos_vec(0), -max_vio_pos_correction);
          vio_pos_x = pos_vec(0) - max_vio_pos_correction;
        }

        // Y position
        if (!std::isfinite(vio_pos_y)) {
          vio_pos_y = 0;
          ROS_ERROR("NaN detected in variable \"vio_pos_y\", setting it to 0 and returning!!!");
          return;
        } else if (vio_pos_y - pos_vec(1) > max_vio_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating VIO Y pos correction %f -> %f", vio_pos_y - pos_vec(1), max_vio_pos_correction);
          vio_pos_y = pos_vec(1) + max_vio_pos_correction;
        } else if (vio_pos_y - pos_vec(1) < -max_vio_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating VIO Y pos correction %f -> %f", vio_pos_y - pos_vec(1), -max_vio_pos_correction);
          vio_pos_y = pos_vec(1) - max_vio_pos_correction;
        }
      }
    }


    // Apply correction step to all state estimators
    stateEstimatorsCorrection(vio_pos_x, vio_pos_y, "pos_vio");

    ROS_WARN_ONCE("[Odometry]: Fusing VIO position");
    //}
  }

  //}

  /* //{ callbackObjectOdometry() */

  void Odometry::callbackObjectOdometry(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    /* ROS_INFO_THROTTLE(1.0, "[Odometry]: Object callback"); */

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackObjectOdometry");

    odom_object_last_update = ros::Time::now();

    double dt;

    if (got_object) {

      {
        std::scoped_lock lock(mutex_odom_object);

        odom_object_previous = odom_object;
        odom_object          = *msg;
        dt                   = (odom_object.header.stamp - odom_object_previous.header.stamp).toSec();
      }

      if (!object_reliable && counter_odom_object > 10) {
        object_reliable = true;
      } else if (counter_odom_object <= 10) {
        counter_odom_object++;
        return;
      }

    } else {

      {
        std::scoped_lock lock(mutex_odom_object);

        odom_object_previous = *msg;
        odom_object          = *msg;
      }

      // Reset estimator
      for (auto &estimator : m_state_estimators) {
        if (std::strcmp(estimator.first.c_str(), "OBJECT") == 0) {
          Eigen::VectorXd pos_vec(2);
          pos_vec(0) = 0.0;
          pos_vec(1) = 0.0;
          estimator.second->setState(0, pos_vec);
        }
      }

      got_object              = true;
      counter_odom_object     = 1;
      odom_object_last_update = ros::Time::now();
      return;
    }


    if (!got_range) {
      return;
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(odom_object.header.stamp.toSec(), odom_object_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Object timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    /* //{ fuse object velocity */

    double vel_object_x, vel_object_y;

    {
      std::scoped_lock lock(mutex_odom_object);

      // TODO find out which one is better
      /* vel_object_x = odom_object.twist.twist.linear.x; */
      /* vel_object_y = odom_object.twist.twist.linear.y; */
      vel_object_x = -(odom_object.pose.pose.position.z - odom_object_previous.pose.pose.position.z) / dt;
      vel_object_y = (odom_object.pose.pose.position.x - odom_object_previous.pose.pose.position.x) / dt;
    }

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(vel_object_x, vel_object_y, "vel_object");

    ROS_WARN_ONCE("[Odometry]: Fusing OBJECT velocity");

    //}

    /* //{ fuse object position */

    double object_pos_x, object_pos_y;

    // Transform to balloon frame
    // Balloons (old, just for transform reference)
    /* double object_pos_x_bal = -odom_object.pose.pose.position.z; */
    /* double object_pos_y_bal = odom_object.pose.pose.position.x; */
    /* double object_pos_z_bal = odom_object.pose.pose.position.y; */

    // Transform from camera frame to fcu frame
    const double offset_bluefox = -0.1;  // TODO move to parameters
    double       x              = -odom_object.pose.pose.position.y + offset_bluefox;
    double       y              = -odom_object.pose.pose.position.x;
    double       z              = odom_object.pose.pose.position.z;

    // Get current UAV attitude
    double                    roll, pitch, yaw;
    geometry_msgs::Quaternion quat;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      quat = odom_pixhawk.pose.pose.orientation;
    }
    tf2::Quaternion qt(quat.x, quat.y, quat.z, quat.w);
    tf2::Matrix3x3(qt).getRPY(roll, pitch, yaw);

    // Precalculate goniometric functions
    double mc = 0.8;  // Works in simulation
    double sy = sin(yaw);
    double cy = cos(yaw);
    double sp = sin(pitch * mc);
    double cp = cos(pitch * mc);
    double sr = sin(roll * mc);
    double cr = cos(roll * mc);

    // Compensate for tilting of the sensor
    object_pos_x = (x * (cy * cp) + y * (cy * sp * sr - sy * cr) + z * (cy * sp * cr + sy * sr));
    object_pos_y = (x * (sy * cp) + y * (sy * sp * sr + cy * cr) + z * (sy * sp * cr - cy * sr));

    // Saturate correction
    for (auto &estimator : m_state_estimators) {
      if (std::strcmp(estimator.first.c_str(), "OBJECT") == 0) {
        Eigen::VectorXd pos_vec(2);
        estimator.second->getState(0, pos_vec);

        // X position
        if (!std::isfinite(object_pos_x)) {
          object_pos_x = 0;
          ROS_ERROR("NaN detected in variable \"object_pos_x\", setting it to 0 and returning!!!");
          return;
        } else if (object_pos_x - pos_vec(0) > max_object_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating OBJECT X pos correction %f -> %f", object_pos_x - pos_vec(0), max_object_pos_correction);
          object_pos_x = pos_vec(0) + max_object_pos_correction;
        } else if (object_pos_x - pos_vec(0) < -max_object_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating OBJECT X pos correction %f -> %f", object_pos_x - pos_vec(0), -max_object_pos_correction);
          object_pos_x = pos_vec(0) - max_object_pos_correction;
        }

        // Y position
        if (!std::isfinite(object_pos_y)) {
          object_pos_y = 0;
          ROS_ERROR("NaN detected in variable \"object_pos_y\", setting it to 0 and returning!!!");
          return;
        } else if (object_pos_y - pos_vec(1) > max_object_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating OBJECT Y pos correction %f -> %f", object_pos_y - pos_vec(1), max_object_pos_correction);
          object_pos_y = pos_vec(1) + max_object_pos_correction;
        } else if (object_pos_y - pos_vec(1) < -max_object_pos_correction) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Saturating OBJECT Y pos correction %f -> %f", object_pos_y - pos_vec(1), -max_object_pos_correction);
          object_pos_y = pos_vec(1) - max_object_pos_correction;
        }
      }
    }


    // Apply correction step to all state estimators
    stateEstimatorsCorrection(object_pos_x, object_pos_y, "pos_object");

    ROS_WARN_ONCE("[Odometry]: Fusing OBJECT position");
    //}
  }

  //}

  /* //{ callbackIcpRelative() */

  void Odometry::callbackIcpRelative(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackIcpRelative");

    icp_odom_last_update = ros::Time::now();

    {
      std::scoped_lock lock(mutex_icp);

      if (got_icp) {

        icp_odom_previous = icp_odom;
        icp_odom          = *msg;

      } else {

        icp_odom_previous = *msg;
        icp_odom          = *msg;

        got_icp = true;
        return;
      }
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(icp_odom.header.stamp.toSec(), icp_odom_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: ICP velocity timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    if (!got_lateral_sensors) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing ICP velocity. Waiting for other sensors.");
      return;
    }

    double vel_icp_x, vel_icp_y;
    {
      std::scoped_lock lock(mutex_icp);

      vel_icp_x = icp_odom.twist.twist.linear.x;
      vel_icp_y = icp_odom.twist.twist.linear.y;
    }

    if (_icp_vel_median_filter) {

      if (!icp_vel_filter_x->isValid(vel_icp_x)) {

        double median = icp_vel_filter_x->getMedian();
        ROS_WARN_THROTTLE(1.0, "[Odometry]: ICP x velocity filtered by median filter. %f -> %f", vel_icp_x, median);
        vel_icp_x = median;
      }

      if (!icp_vel_filter_y->isValid(vel_icp_y)) {

        double median = icp_vel_filter_y->getMedian();
        ROS_WARN_THROTTLE(1.0, "[Odometry]: ICP y velocity filtered by median filter. %f -> %f", vel_icp_y, median);
        vel_icp_y = median;
      }
    }

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(vel_icp_x, vel_icp_y, "vel_icp");

    ROS_WARN_ONCE("[Odometry]: Fusing ICP velocity");
  }
  //}

  /* //{ callbackIcpAbsolute() */

  void Odometry::callbackIcpAbsolute(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackIcpAbsolute");

    icp_global_odom_last_update = ros::Time::now();

    {
      std::scoped_lock lock(mutex_icp_global);

      if (got_icp_global) {

        icp_global_odom_previous = icp_global_odom;
        icp_global_odom          = *msg;

      } else {

        icp_global_odom_previous = *msg;
        icp_global_odom          = *msg;

        got_icp_global = true;
        return;
      }
    }

    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    if (!isTimestampOK(icp_global_odom.header.stamp.toSec(), icp_global_odom_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: ICP global odom timestamp not OK, not fusing correction.");
      return;
    }

    //////////////////// Fuse Lateral Kalman ////////////////////

    if (!got_lateral_sensors) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Not fusing ICP position. Waiting for other sensors.");
      return;
    }

    double pos_icp_x, pos_icp_y;

    {
      std::scoped_lock lock(mutex_icp_global);

      pos_icp_x = icp_global_odom.pose.pose.position.x;
      pos_icp_y = icp_global_odom.pose.pose.position.y;
    }

    // Apply correction step to all state estimators
    stateEstimatorsCorrection(pos_icp_x, pos_icp_y, "pos_icp");

    ROS_WARN_ONCE("[Odometry]: Fusing ICP position");
  }
  //}

  /* //{ callbackOptflowStddev() */
  void Odometry::callbackOptflowStddev(const geometry_msgs::Vector3ConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackOptflowStd");

    {
      std::scoped_lock lock(mutex_optflow_stddev);

      optflow_stddev = *msg;
    }
  }
  //}

  /* //{ callbackTeraranger() */

  void Odometry::callbackTeraranger(const sensor_msgs::RangeConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackTeraranger");

    if (got_range) {
      {
        std::scoped_lock lock(mutex_range_terarangerone);
        range_terarangerone_previous = range_terarangerone_;
        range_terarangerone_         = *msg;
      }
    } else {
      std::scoped_lock lock(mutex_range_terarangerone);
      {
        range_terarangerone_previous = *msg;
        range_terarangerone_         = *msg;
      }
      got_range = true;
    }

    if (!got_odom_pixhawk) {

      return;
    }

    // getting roll, pitch, yaw
    double                    roll, pitch, yaw;
    geometry_msgs::Quaternion quat;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      quat = odom_pixhawk.pose.pose.orientation;
    }
    tf2::Quaternion qt(quat.x, quat.y, quat.z, quat.w);
    tf2::Matrix3x3(qt).getRPY(roll, pitch, yaw);

    double measurement = 0;
    // compensate for tilting of the sensor
    measurement = range_terarangerone_.range * cos(roll) * cos(pitch) + trg_z_offset_;

    if (!std::isfinite(measurement)) {

      ROS_ERROR_THROTTLE(1, "[Odometry]: NaN detected in Teraranger variable \"measurement\" (teraranger)!!!");
      return;
    }

    got_range = true;

    // deside on measurement's covariance
    Eigen::MatrixXd mesCov;
    mesCov = Eigen::MatrixXd::Zero(altitude_p, altitude_p);

    // teraranger filtration
    if (isUavFlying()) {

      if (!terarangerFilter->isValid(measurement)) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Teraranger measurement %f declined by median filter.", measurement);
        return;
      }
    }

    //////////////////// Fuse main altitude kalman ////////////////////
    if (teraranger_enabled) {

      // fuse the measurement only when terarangerFilter produced positive value, i.e. feasible value
      if (measurement > 0.2) {
        Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(altitude_n, 1);
        if (!current_alt_estimator->getStates(current_altitude)) {
          ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator not initialized.");
          return;
        }

        // create a correction value
        double correction;
        correction = measurement - current_altitude(2);

        // saturate the correction
        if (!std::isfinite(correction)) {
          correction = 0;
          ROS_ERROR("[Odometry]: NaN detected in Teraranger variable \"correction\", setting it to 0!!!");
        } else if (correction > max_altitude_correction_) {
          correction = max_altitude_correction_;
        } else if (correction < -max_altitude_correction_) {
          correction = -max_altitude_correction_;
        }

        // set the measurement vector
        double height_range;
        height_range = current_altitude(2) + correction;

        {
          std::scoped_lock lock(mutex_altitude_estimator);
          altitudeEstimatorCorrection(height_range, "height_range");
          if (fabs(height_range) > 100) {
            ROS_WARN("Teraranger height correction: %f", height_range);
          }
        }

        ROS_WARN_ONCE("[Odometry]: fusing Teraranger rangefinder");
      }
    }
  }

  //}

  /* //{ callbackGarmin() */

  void Odometry::callbackGarmin(const sensor_msgs::RangeConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackGarmin");

    if (got_range) {
      {
        std::scoped_lock lock(mutex_range_garmin);
        range_garmin_previous = range_garmin;
        range_garmin          = *msg;
      }
    } else {
      std::scoped_lock lock(mutex_range_garmin);
      {
        range_garmin_previous = *msg;
        range_garmin          = *msg;
      }
      got_range = true;
    }

    if (!isTimestampOK(range_garmin.header.stamp.toSec(), range_garmin_previous.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Garmin range timestamp not OK, not fusing correction.");
      return;
    }

    if (!got_odom_pixhawk) {
      return;
    }


    double                    roll, pitch, yaw;
    geometry_msgs::Quaternion quat;
    {
      std::scoped_lock lock(mutex_odom_pixhawk);
      quat = odom_pixhawk.pose.pose.orientation;
    }
    tf2::Quaternion qt(quat.x, quat.y, quat.z, quat.w);
    tf2::Matrix3x3(qt).getRPY(roll, pitch, yaw);

    // Check for excessive tilts
    if (std::fabs(roll) > _excessive_tilt || std::fabs(pitch) > _excessive_tilt) {
      excessive_tilt = true;
    } else {
      excessive_tilt = false;
    }


    double range_fcu = 0;
    {
      std::scoped_lock lock(mutex_range_garmin);
      try {
        const ros::Duration             timeout(1.0 / 100.0);
        geometry_msgs::TransformStamped tf_fcu2garmin =
            m_tf_buffer.lookupTransform("fcu_" + uav_name, range_garmin.header.frame_id, range_garmin.header.stamp, timeout);
        range_fcu = range_garmin.range - tf_fcu2garmin.transform.translation.z + tf_fcu2garmin.transform.translation.x * tan(pitch) +
                    tf_fcu2garmin.transform.translation.y * tan(roll);
      }
      catch (tf2::TransformException &ex) {
        ROS_WARN_THROTTLE(10.0, "Error during transform from \"%s\" frame to \"%s\" frame. Using offset from config file instead. \n\tMSG: %s",
                          range_garmin.header.frame_id.c_str(), ("fcu_" + uav_name).c_str(), ex.what());
        range_fcu = range_garmin.range + garmin_z_offset_;
      }
    }

    double measurement = range_fcu * cos(roll) * cos(pitch);

    if (!std::isfinite(measurement)) {

      ROS_ERROR_THROTTLE(1, "[Odometry]: NaN detected in Garmin variable \"measurement\" (garmin)!!!");

      return;
    }

    got_range = true;

    // deside on measurement's covariance
    Eigen::MatrixXd mesCov;
    mesCov = Eigen::MatrixXd::Zero(altitude_p, altitude_p);

    //////////////////// Filter out garmin measurement ////////////////////
    // garmin filtration
    if (isUavFlying()) {
      if (!garminFilter->isValid(measurement)) {
        double filtered = garminFilter->getMedian();
        /* ROS_WARN_THROTTLE(1.0, "[Odometry]: Garmin measurement %f -> %f filtered by median filter.", measurement, filtered); */
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Garmin measurement %f declined by median filter.", measurement);
        /* measurement = filtered; */
        return;
      }
    }

    //////////////////// Fuse main altitude kalman ////////////////////
    if (!garmin_enabled) {
      ROS_WARN_ONCE("[Odometry]: Garmin not enabled. Not fusing range corrections.");
      return;
    }

    if (measurement < 0.01) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Garmin measurement %f < %f. Not fusing.", measurement, 0.01);
      return;
    }

    if (measurement > garmin_max_valid_altitude) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Garmin measurement %f > %f. Not fusing.", measurement, garmin_max_valid_altitude);
      return;
    }

    if (excessive_tilt) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Excessive tilt detected - roll: %f, pitch: %f. Not fusing.", roll, pitch);
      return;
    }

    // Fuse garmin measurement for each altitude estimator
    for (auto &estimator : m_altitude_estimators) {
      Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(altitude_n, 1);
      if (!estimator.second->getStates(current_altitude)) {
        ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator not initialized.");
        return;
      }
      /* ROS_WARN_THROTTLE(1.0, "Garmin measurement: %f", measurement); */
      // create a correction value
      double correction;
      correction = measurement - current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT);
      /* ROS_WARN_THROTTLE(1.0, "Garmin correction: %f", correction); */

      // saturate the correction
      if (!std::isfinite(correction)) {
        correction = 0;
        ROS_ERROR("[Odometry]: NaN detected in Garmin variable \"correction\", setting it to 0!!!");
      } else if (correction > max_altitude_correction_) {
        correction = max_altitude_correction_;
      } else if (correction < -max_altitude_correction_) {
        correction = -max_altitude_correction_;
      }

      // set the measurement vector
      double height_range, elevation;
      height_range = current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT) + correction;

      {
        std::scoped_lock lock(mutex_altitude_estimator);
        altitudeEstimatorCorrection(height_range, "height_range", estimator.second);
        if (fabs(height_range) > 100) {
          ROS_WARN("Garmin height correction: %f", height_range);
        }
        estimator.second->getStates(current_altitude);
        if (std::strcmp(estimator.second->getName().c_str(), "HEIGHT") == 0) {
          /* ROS_WARN_THROTTLE(1.0, "Garmin altitude correction: %f", height_range); */
          /* ROS_WARN_THROTTLE(1.0, "Height after correction: %f", current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT)); */
        }
      }

      if (estimate_elevation && std::strcmp(estimator.first.c_str(), "ELEVATION") == 0) {
        /* elevation         = current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE) - current_altitude(mrs_msgs::AltitudeStateNames::HEIGHT); */
        /* double innovation = elevation - current_altitude(mrs_msgs::AltitudeStateNames::ELEVATION); */

        /* publish innovation //{ */


        /* std::map<std::string, int>::iterator it_measurement_id = map_alt_measurement_name_id.find("elevation"); */

        /* Eigen::VectorXd innovation_vec = Eigen::VectorXd::Zero(1); */
        /* Eigen::VectorXd elevation_vec  = Eigen::VectorXd::Zero(1); */
        /* elevation_vec(0)               = elevation; */
        /* { */
        /*   std::scoped_lock lock(mutex_altitude_estimator); */
        /*   current_alt_estimator->getInnovation(elevation_vec, it_measurement_id->second, innovation_vec); */
        /* } */
        /* mrs_msgs::Float64Stamped inno_elevation; */
        /* inno_elevation.header.stamp    = ros::Time::now(); */
        /* inno_elevation.header.frame_id = "local_origin"; */
        /* inno_elevation.value           = innovation_vec(0); */
        /* try { */
        /*   pub_inno_elevation_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(inno_elevation))); */
        /* } */
        /* catch (...) { */
        /*   ROS_ERROR("Exception caught during publishing topic %s.", pub_inno_elevation_.getTopic().c_str()); */
        /* } */

        /* double innovation_stddev = stddev_inno_elevation->getStddev(innovation); */

        /* publish innovation stddev //{ */

        /* mrs_msgs::Float64Stamped inno_stddev_msg; */
        /* inno_stddev_msg.header.stamp    = ros::Time::now(); */
        /* inno_stddev_msg.header.frame_id = "local_origin"; */
        /* inno_stddev_msg.value           = innovation_stddev; */
        /* try { */
        /*   pub_inno_stddev_elevation_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(inno_stddev_msg))); */
        /* } */
        /* catch (...) { */
        /*   ROS_ERROR("Exception caught during publishing topic %s.", pub_inno_stddev_elevation_.getTopic().c_str()); */
        /* } */

        //}

        //}

        /* publish innovation covariance //{ */


        /* Eigen::MatrixXd innovation_cov = Eigen::MatrixXd::Zero(1, 1); */
        /* { */
        /*   std::scoped_lock lock(mutex_altitude_estimator); */
        /*   current_alt_estimator->getInnovationCovariance(it_measurement_id->second, innovation_cov); */
        /* } */
        /* mrs_msgs::Float64Stamped inno_cov_msg; */
        /* inno_cov_msg.header.stamp    = ros::Time::now(); */
        /* inno_cov_msg.header.frame_id = "local_origin"; */
        /* inno_cov_msg.value           = innovation_cov(0, 0); */
        /* try { */
        /*   pub_inno_cov_elevation_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(inno_cov_msg))); */
        /* } */
        /* catch (...) { */
        /*   ROS_ERROR("Exception caught during publishing topic %s.", pub_inno_cov_elevation_.getTopic().c_str()); */
        /* } */


        //}

        /* double veldiff        = current_altitude(mrs_msgs::AltitudeStateNames::VEL_ALT) - current_altitude(mrs_msgs::AltitudeStateNames::VEL_HEIGHT); */
        /* double veldiff_stddev = 0.0; */

        // Start estimating stddev of velocity difference when we are in the air
        /* if (current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE) > _elevation_tolerance) { */
        /* if (isUavFlying() && !isUavLandoff()) { */
        /*   veldiff_stddev = stddev_veldiff->getStddev(veldiff); */
        /* } */

        /* publish innovation stddev //{ */

        /* mrs_msgs::Float64Stamped veldiff_stddev_msg; */
        /* veldiff_stddev_msg.header.stamp    = ros::Time::now(); */
        /* veldiff_stddev_msg.header.frame_id = "local_origin"; */
        /* veldiff_stddev_msg.value           = veldiff_stddev; */
        /* try { */
        /*   pub_veldiff_stddev_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(veldiff_stddev_msg))); */
        /* } */
        /* catch (...) { */
        /*   ROS_ERROR("Exception caught during publishing topic %s.", pub_veldiff_stddev_.getTopic().c_str()); */
        /* } */

        //}

        // We want to detect flying above obstacle when the elevation innovation exceeds 3 standard deviations
        /* if (stddev_veldiff->hasEnoughSamples() && !obstacle_detected && current_altitude(mrs_msgs::AltitudeStateNames::ALTITUDE) > _elevation_tolerance &&
         */
        /*     std::pow(innovation, 2) > std::pow(3 * innovation_stddev, 2)) { */
        /*   obstacle_detected = true; */
        /* } */
        /* if (obstacle_detected && std::pow(innovation, 2) < std::pow(1 * innovation_stddev, 2)) { */
        /*   obstacle_detected = false; */
        /* } */

        /* if (estimate_elevation && obstacle_detected) { */
        /*   std::scoped_lock lock(mutex_altitude_estimator); */
        /*   altitudeEstimatorCorrection(elevation, "elevation"); */
        /*   /1* ROS_WARN_THROTTLE(1.0, "Elevation correction: %f", elevation); *1/ */
        /* } */
      }
    }

    ROS_WARN_ONCE("[Odometry]: fusing Garmin rangefinder");
  }

  //}

  /* //{ callbackPixhawkUtm() */

  void Odometry::callbackPixhawkUtm(const sensor_msgs::NavSatFixConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackPixhawkUtm");

    double out_x;
    double out_y;

    mrs_lib::UTM(msg->latitude, msg->longitude, &out_x, &out_y);

    if (!std::isfinite(out_x)) {
      ROS_ERROR("[Odometry]: NaN detected in UTM variable \"out_x\"!!!");
      return;
    }

    if (!std::isfinite(out_y)) {
      ROS_ERROR("[Odometry]: NaN detected in UTM variable \"out_y\"!!!");
      return;
    }

    {
      std::scoped_lock lock(mutex_pixhawk_utm_position);

      pixhawk_utm_position_x = out_x;
      pixhawk_utm_position_y = out_y;
    }

    got_pixhawk_utm = true;

    nav_msgs::Odometry gps_local_odom;
    gps_local_odom.header          = msg->header;
    gps_local_odom.header.frame_id = "local_origin";

    // | ------------- offset the gps to local_origin ------------- |
    gps_local_odom.pose.pose.position.x = pixhawk_utm_position_x - utm_origin_x_;
    gps_local_odom.pose.pose.position.y = pixhawk_utm_position_y - utm_origin_y_;

    {
      std::scoped_lock lock(mutex_odom_pixhawk);

      gps_local_odom.pose.pose.position.z  = odom_pixhawk.pose.pose.position.z;
      gps_local_odom.pose.pose.orientation = odom_pixhawk.pose.pose.orientation;
      gps_local_odom.twist                 = odom_pixhawk.twist;
    }

    // | ------------- publish the gps local odometry ------------- |
    {
      std::scoped_lock lock(mutex_gps_local_odom);

      try {
        pub_gps_local_odom.publish(gps_local_odom);
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_gps_local_odom.getTopic().c_str());
      }
    }
  }

  //}

  /* //{ callbackTrackerStatus() */

  void Odometry::callbackTrackerStatus(const mrs_msgs::TrackerStatusConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackTrackerStatus");

    std::scoped_lock lock(mutex_tracker_status);

    tracker_status     = *msg;
    got_tracker_status = true;

    if (uav_in_the_air && tracker_status.tracker.compare(null_tracker_) == STRING_EQUAL) {

      // save the current position
      // TODO this might be too simple solution
      Eigen::VectorXd pose = Eigen::VectorXd::Zero(2);
      current_estimator->getState(0, pose);
      land_position_x   = pose[0];
      land_position_y   = pose[1];
      land_position_set = true;

      uav_in_the_air = false;
    } else if (!uav_in_the_air && tracker_status.tracker.compare(null_tracker_) != STRING_EQUAL) {
      uav_in_the_air = true;
    }
  }
  //}

  /* //{ callbackMavrosDiag() */
  void Odometry::callbackMavrosDiag(const mrs_msgs::MavrosDiagnosticsConstPtr &msg) {

    if (!is_initialized)
      return;

    static ros::Time t_start = ros::Time::now();

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackMavrosDiag");

    max_altitude = _max_default_altitude;

    {
      std::scoped_lock lock(mutex_mavros_diag);

      mavros_diag.gps.satellites_visible = msg->gps.satellites_visible;
    }

    if (gps_reliable && mavros_diag.gps.satellites_visible < _min_satellites) {

      gps_reliable = false;
      ROS_WARN("[Odometry]: GPS unreliable. %d satellites visible.", mavros_diag.gps.satellites_visible);

      // If optflow is available, prepare to switching to OPTFLOW estimator by decreasing the max altitude
      /* if (_optflow_available) { */
      /*   max_altitude = _max_optflow_altitude; */
      /*   ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude); */
      /* } */

    } else if (_gps_available && !gps_reliable && mavros_diag.gps.satellites_visible >= _min_satellites) {

      gps_reliable = true;
      ROS_WARN("[Odometry]: GPS reliable. %d satellites visible.", mavros_diag.gps.satellites_visible);

      // Change the maximum altitude back to default if the current estimator is not OPTFLOW
      if (_estimator_type.type != mrs_msgs::EstimatorType::OPTFLOW) {
        max_altitude = _max_default_altitude;
        ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);
      }
    }

    ROS_INFO_THROTTLE(
        10.0, "[Odometry]: Running for %.2f seconds. Lateral estimator: %s, Altitude estimator: %s, Heading estimator: %s, Max altitude: %f, Satellites: %d",
        (ros::Time::now() - t_start).toSec(), current_estimator_name.c_str(), current_alt_estimator_name.c_str(), current_hdg_estimator_name.c_str(),
        max_altitude, mavros_diag.gps.satellites_visible);
  }
  //}

  /* callbackVioState() //{ */

  void Odometry::callbackVioState(const std_msgs::Bool &msg) {

    if (!is_initialized)
      return;

    if (msg.data) {
      if (_is_estimator_tmp) {
        vio_reliable      = true;
        _is_estimator_tmp = false;
        mrs_msgs::EstimatorType desired_estimator;
        desired_estimator.type = mrs_msgs::EstimatorType::VIO;
        if (changeCurrentEstimator(desired_estimator)) {
          ROS_WARN("[Odometry]: VIO tracking resumed. Current estimator: VIO");
          return;
        }
      }
    } else {
      if (std::strcmp(current_estimator->getName().c_str(), "VIO") == 0 && !_is_estimator_tmp) {
        vio_reliable = false;

        mrs_msgs::EstimatorType tmp_estimator;
        tmp_estimator.type = mrs_msgs::EstimatorType::OPTFLOW;
        if (changeCurrentEstimator(tmp_estimator)) {
          _is_estimator_tmp = true;
          ROS_WARN("[Odometry]: VIO tracking lost. Temporary state: OPTFLOW");
          return;
        } else {
          tmp_estimator.type = mrs_msgs::EstimatorType::GPS;
          if (changeCurrentEstimator(tmp_estimator)) {
            _is_estimator_tmp = true;
            ROS_WARN("[Odometry]: VIO tracking lost. Temporary state: GPS");
            return;
          }
        }
        ROS_ERROR("[Odometry]: VIO tracking lost and neither OPTFLOW or GPS reliable. Should not happen.");
      }
    }
  }

  //}

  /* //{ callbackGroundTruth() */
  void Odometry::callbackGroundTruth(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackGroundTruth");

    {
      std::scoped_lock lock(mutex_ground_truth);

      ground_truth = *msg;
    }

    // rotations in inertial frame
    double rot_x, rot_y, rot_z;
    getGlobalRot(msg->pose.pose.orientation, rot_x, rot_y, rot_z);

    orientation_gt.header   = odom_pixhawk.header;
    orientation_gt.vector.x = rot_x;
    orientation_gt.vector.y = rot_y;
    orientation_gt.vector.z = rot_z;

    try {
      pub_orientation_gt_.publish(orientation_gt);
    }
    catch (...) {
      ROS_ERROR("Exception caught during publishing topic %s.", pub_orientation_gt_.getTopic().c_str());
    }
  }
  //}

  /* //{ callbackT265Odometry() */
  void Odometry::callbackT265Odometry(const nav_msgs::OdometryConstPtr &msg) {

    if (!is_initialized)
      return;

    mrs_lib::Routine profiler_routine = profiler->createRoutine("callbackT265Odometry");

    if (got_odom_t265) {

      {
        std::scoped_lock lock(mutex_odom_t265);

        odom_t265_previous = odom_t265;
        odom_t265          = *msg;
      }

    } else {

      {
        std::scoped_lock lock(mutex_odom_t265);

        odom_t265_previous = *msg;
        odom_t265          = *msg;
      }

      got_odom_t265         = true;
      odom_t265_last_update = ros::Time::now();
      return;
    }

    odom_t265_last_update = ros::Time::now();

    if (!isTimestampOK(odom_t265.header.stamp.toSec(), odom_t265.header.stamp.toSec())) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: T265 odom timestamp not OK.");
    }

    double dt;
    {
      std::scoped_lock lock(mutex_odom_t265);

      dt = (odom_t265.header.stamp - odom_pixhawk_previous.header.stamp).toSec();
    }

    if (!got_init_heading) {

      // Pixhawk (compass) orientation
      tf2::Quaternion q;
      {
        std::scoped_lock lock(mutex_odom_t265);

        q.setX(odom_t265.pose.pose.orientation.x);
        q.setY(odom_t265.pose.pose.orientation.y);
        q.setZ(odom_t265.pose.pose.orientation.z);
        q.setW(odom_t265.pose.pose.orientation.w);
      }
      q.normalize();

      double roll, pitch;
      tf2::Matrix3x3(q).getRPY(roll, pitch, m_init_heading);
      got_init_heading = true;
    }

    if (!got_range) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Waiting for rangefinder.");
      return;
    }


    // --------------------------------------------------------------
    // |                        callback body                       |
    // --------------------------------------------------------------

    /* check NaNs //{ */
    if (t265_reliable && !std::isfinite(odom_t265.pose.pose.position.x)) {
      odom_t265.pose.pose.position.x = 0;
      ROS_ERROR("NaN detected in variable \"odom_t265.pose.pose.position.x\", T265 odom is now unreliable!!!");
      t265_reliable = false;
      return;
    }

    if (t265_reliable && !std::isfinite(odom_t265.pose.pose.position.y)) {
      odom_t265.pose.pose.position.y = 0;
      ROS_ERROR("NaN detected in variable \"odom_t265.pose.pose.position.y\", T265 odom is now unreliable!!!");
      t265_reliable = false;
      return;
    }

    if (t265_reliable && !std::isfinite(odom_t265.pose.pose.position.z)) {
      odom_t265.pose.pose.position.z = 0;
      ROS_ERROR("NaN detected in variable \"odom_t265.pose.pose.position.z\", T265 odom is now unreliable!!!");
      t265_reliable = false;
      return;
    }
    //}

    /* check maximum velocity //{ */

    double vel_x, vel_y, vel_z;
    {
      std::scoped_lock lock(mutex_odom_t265);
      vel_x = (odom_t265.pose.pose.position.x - odom_t265_previous.pose.pose.position.x) / dt;
      vel_y = (odom_t265.pose.pose.position.y - odom_t265_previous.pose.pose.position.y) / dt;
      vel_z = (odom_t265.pose.pose.position.z - odom_t265_previous.pose.pose.position.z) / dt;
    }

    if (vel_x > _max_t265_vel || vel_y > _max_t265_vel || vel_z > _max_t265_vel) {
      t265_reliable = false;
      ROS_WARN_THROTTLE(1.0, "[Odometry]: T265 velocity: x: %f, y: %f, z: %f, exceeded %f m/s", vel_x, vel_y, vel_z, _max_t265_vel);
      return;
    }

    //}

    /* republish t265 odometry //{ */

    if (std::strcmp(current_estimator_name.c_str(), "T265") == STRING_EQUAL) {

      /* publish t265 altitude //{ */
      mrs_msgs::Float64Stamped new_altitude;
      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        new_altitude.header = odom_t265.header;
        new_altitude.value  = odom_t265.pose.pose.position.z;
      }

      new_altitude.header.frame_id = "local_origin";
      new_altitude.header.stamp    = ros::Time::now();

      try {
        pub_altitude_.publish(mrs_msgs::Float64StampedConstPtr(new mrs_msgs::Float64Stamped(new_altitude)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_altitude_.getTopic().c_str());
      }
      //}

      /* publish t265 orientation  //{ */

      nav_msgs::Odometry orientation;
      {
        std::scoped_lock lock(mutex_odom_t265);
        orientation.header                = odom_t265.header;
        orientation.pose.pose.orientation = odom_t265.pose.pose.orientation;
      }
      orientation.header.frame_id = "local_origin";

      try {
        pub_orientation_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(orientation)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_orientation_.getTopic().c_str());
      }

      //}

      /* publish t265 odometry //{ */
      nav_msgs::Odometry odom_main;
      {
        std::scoped_lock lock(mutex_odom_t265);
        odom_main = odom_t265;
      }

      odom_main.header.frame_id = "local_origin";
      odom_main.header.stamp    = ros::Time::now();

      if (!odometry_published) {
        odom_stable                         = odom_main;
        odom_stable.pose.pose.orientation.x = 0.0;
        odom_stable.pose.pose.orientation.y = 0.0;
        odom_stable.pose.pose.orientation.z = 0.0;
        odom_stable.pose.pose.orientation.w = 1.0;
        m_pos_odom_offset.setZero();
        m_rot_odom_offset = tf2::Quaternion(0.0, 0.0, 0.0, 1.0);
        m_rot_odom_offset.normalize();
      }

      if (std::strcmp(odom_main.child_frame_id.c_str(), odom_stable.child_frame_id.c_str()) != STRING_EQUAL) {

        tf2::Vector3 v1, v2;
        tf2::fromMsg(odom_main.pose.pose.position, v1);
        tf2::fromMsg(odom_stable.pose.pose.position, v2);
        tf2::Vector3 pos_diff = v1 - v2;
        m_pos_odom_offset     = pos_diff;

        // Somehow the odom_stable quaternion becomes (0.0, 0.0, 0.0, 0.0)
        if (odom_stable.pose.pose.orientation.w == 0.0) {
          /* odom_stable.pose.pose.orientation.w = 1.0; */
          odom_stable.pose.pose.orientation = odom_pixhawk.pose.pose.orientation;
        }
        tf2::Quaternion q1, q2;
        tf2::fromMsg(odom_main.pose.pose.orientation, q1);
        tf2::fromMsg(odom_stable.pose.pose.orientation, q2);
        tf2::Quaternion rot_diff = q2 * q1.inverse();
        m_rot_odom_offset        = rot_diff;
        m_rot_odom_offset.normalize();
        /* ROS_WARN("[Odometry]: odometry change stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
         * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
        /* ROS_WARN("[Odometry]: q1: %f, %f, %f, %f,\t q2: %f, %f, %f, %f", q1.x(), q1.y(), q1.z(), q1.w(), q2.x(), q2.y(), q2.z(), q2.w()); */
        ROS_WARN("[Odometry]: Changed odometry estimator. Updating offset for stable odometry.");
      }

      /* ROS_WARN("[Odometry]: before stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
       * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
      odom_stable = applyOdomOffset(odom_main);
      /* ROS_WARN("[Odometry]: after stable_q: %f, %f, %f, %f", odom_stable.pose.pose.orientation.x, odom_stable.pose.pose.orientation.y,
       * odom_stable.pose.pose.orientation.z, odom_stable.pose.pose.orientation.w); */
      odom_stable.header.frame_id = "local_origin_stable";

      try {
        pub_odom_stable_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(odom_stable)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odom_stable_.getTopic().c_str());
      }

      // publish TF
      geometry_msgs::TransformStamped tf_stable;
      tf_stable.header.stamp          = ros::Time::now();
      tf_stable.header.frame_id       = "local_origin_stable";
      tf_stable.child_frame_id        = "local_origin";
      tf_stable.transform.translation = tf2::toMsg(tf2::Vector3(0.0, 0.0, 0.0) - m_pos_odom_offset);
      tf_stable.transform.rotation    = tf2::toMsg(m_rot_odom_offset.inverse());
      try {
        broadcaster_->sendTransform(tf_stable);
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing TF: %s - %s.", tf_stable.child_frame_id.c_str(), tf_stable.header.frame_id.c_str());
      }

      {
        std::scoped_lock lock(mutex_shared_odometry);

        shared_odom = odom_main;
      }

      try {
        pub_odom_main_.publish(nav_msgs::OdometryConstPtr(new nav_msgs::Odometry(odom_main)));
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing topic %s.", pub_odom_main_.getTopic().c_str());
      }
      ROS_INFO_ONCE("[Odometry]: Publishing odometry");

      // publish TF
      geometry_msgs::Vector3 position;
      position.x = odom_main.pose.pose.position.x;
      position.y = odom_main.pose.pose.position.y;
      position.z = odom_main.pose.pose.position.z;
      geometry_msgs::TransformStamped tf;
      tf.header.stamp          = ros::Time::now();
      tf.header.frame_id       = "local_origin";
      tf.child_frame_id        = std::string("fcu_") + uav_name;
      tf.transform.translation = position;
      tf.transform.rotation    = odom_main.pose.pose.orientation;
      try {
        broadcaster_->sendTransform(tf);
      }
      catch (...) {
        ROS_ERROR("[Odometry]: Exception caught during publishing TF: %s - %s.", tf.child_frame_id.c_str(), tf.header.frame_id.c_str());
      }
      //}
    }
    //}
  }

  //}

  // | -------------------- service callbacks ------------------- |

  /* //{ TODO callbackToggleRtkHeight() - not supported now */

  /* bool Odometry::callbackToggleRtkHeight(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res) { */

  /*   if (!is_initialized) */
  /*     return false; */

  /*   // set the intergrated altitude to the current altitude from kalman */
  /*   { */
  /*     std::scoped_lock lock(mutex_altitude_estimator); */

  /*     rtk_altitude_integral = main_altitude_kalman->getState(0); */
  /*   } */

  /*   rtk_altitude_enabled = req.data; */

  /*   res.success = true; */
  /*   res.message = (rtk_altitude_enabled ? "RTK altitude enabled" : "RTK altitude disabled"); */

  /*   if (rtk_altitude_enabled) { */

  /*     ROS_INFO("[Odometry]: Rtk altitude enabled."); */
  /*     teraranger_enabled = false; */
  /*     garmin_enabled     = false; */
  /*     /1* object_altitude_enabled = false; *1/ */

  /*   } else { */

  /*     ROS_INFO("[Odometry]: Rtk altitude disabled."); */
  /*   } */

  /*   return true; */
  /* } */

  //}

  /* //{ callbackChangeEstimator() */

  bool Odometry::callbackChangeEstimator(mrs_msgs::ChangeEstimator::Request &req, mrs_msgs::ChangeEstimator::Response &res) {

    if (!is_initialized)
      return false;

    // Check whether a valid type was requested
    if (!isValidType(req.estimator_type)) {
      ROS_ERROR("[Odometry]: %d is not a valid odometry type", req.estimator_type.type);
      res.success = false;
      res.message = ("Not a valid odometry type");
      {
        std::scoped_lock lock(mutex_estimator_type);

        res.estimator_type.type = _estimator_type.type;
      }
      return true;
    }

    bool success = false;
    {
      std::scoped_lock lock(mutex_estimator_type);

      mrs_msgs::EstimatorType desired_estimator;
      desired_estimator.type = req.estimator_type.type;
      desired_estimator.name = _state_estimators_names[desired_estimator.type];
      success                = changeCurrentEstimator(desired_estimator);
    }

    ROS_INFO("[Odometry]: %s", printOdometryDiag().c_str());

    res.success = success;
    res.message = (printOdometryDiag().c_str());
    {
      std::scoped_lock lock(mutex_estimator_type);

      res.estimator_type.type = _estimator_type.type;
    }

    return true;
  }

  //}

  /* //{ callbackChangeEstimatorString() */

  bool Odometry::callbackChangeEstimatorString(mrs_msgs::String::Request &req, mrs_msgs::String::Response &res) {

    if (!is_initialized)
      return false;

    mrs_msgs::EstimatorType desired_estimator;

    std::string type = req.value;
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);
    if (std::strcmp(type.c_str(), "OPTFLOW") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::OPTFLOW;
    } else if (std::strcmp(type.c_str(), "GPS") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::GPS;
    } else if (std::strcmp(type.c_str(), "OPTFLOWGPS") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::OPTFLOWGPS;
    } else if (std::strcmp(type.c_str(), "RTK") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::RTK;
    } else if (std::strcmp(type.c_str(), "ICP") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::ICP;
    } else if (std::strcmp(type.c_str(), "VIO") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::VIO;
    } else if (std::strcmp(type.c_str(), "OBJECT") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::OBJECT;
    } else if (std::strcmp(type.c_str(), "T265") == 0) {
      desired_estimator.type = mrs_msgs::EstimatorType::T265;
    } else {
      ROS_WARN("[Odometry]: Invalid type %s requested", type.c_str());
      res.success = false;
      res.message = ("Not a valid odometry type");
      return true;
    }

    // Check whether a valid type was requested
    if (!isValidType(desired_estimator)) {
      ROS_ERROR("[Odometry]: %d is not a valid odometry type", desired_estimator.type);
      res.success = false;
      res.message = ("Not a valid odometry type");
      return true;
    }

    desired_estimator.name = _state_estimators_names[desired_estimator.type];

    bool success = false;
    {
      std::scoped_lock lock(mutex_estimator_type);

      success = changeCurrentEstimator(desired_estimator);
    }

    ROS_INFO("[Odometry]: %s", printOdometryDiag().c_str());

    res.success = success;
    res.message = (printOdometryDiag().c_str());

    return true;
  }  // namespace mrs_odometry

  //}

  /* //{ callbackChangeHdgEstimator() */

  bool Odometry::callbackChangeHdgEstimator(mrs_msgs::ChangeHdgEstimator::Request &req, mrs_msgs::ChangeHdgEstimator::Response &res) {

    if (!is_initialized)
      return false;

    // Check whether a valid type was requested
    if (!isValidType(req.estimator_type)) {
      ROS_ERROR("[Odometry]: %d is not a valid heading estimator type", req.estimator_type.type);
      res.success = false;
      res.message = ("Not a valid heading estimator type");
      {
        std::scoped_lock lock(mutex_hdg_estimator_type);

        res.estimator_type.type = _hdg_estimator_type.type;
      }
      return true;
    }

    bool success = false;
    {
      std::scoped_lock lock(mutex_hdg_estimator_type);

      mrs_msgs::HeadingType desired_estimator;
      desired_estimator.type = req.estimator_type.type;
      desired_estimator.name = _heading_estimators_names[desired_estimator.type];
      success                = changeCurrentHeadingEstimator(desired_estimator);
    }

    ROS_INFO("[Odometry]: %s", printOdometryDiag().c_str());

    res.success = success;
    res.message = (printOdometryDiag().c_str());
    {
      std::scoped_lock lock(mutex_hdg_estimator_type);

      res.estimator_type.type = _hdg_estimator_type.type;
    }

    return true;
  }

  //}

  /* //{ callbackChangeHdgEstimatorString() */

  bool Odometry::callbackChangeHdgEstimatorString(mrs_msgs::String::Request &req, mrs_msgs::String::Response &res) {

    if (!is_initialized)
      return false;

    mrs_msgs::HeadingType desired_estimator;

    std::string type = req.value;
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);
    if (std::strcmp(type.c_str(), "GYRO") == 0) {
      desired_estimator.type = mrs_msgs::HeadingType::GYRO;
    } else if (std::strcmp(type.c_str(), "COMPASS") == 0) {
      desired_estimator.type = mrs_msgs::HeadingType::COMPASS;
    } else if (std::strcmp(type.c_str(), "OPTFLOW") == 0) {
      desired_estimator.type = mrs_msgs::HeadingType::OPTFLOW;
    } else {
      ROS_WARN("[Odometry]: Invalid type %s requested", type.c_str());
      res.success = false;
      res.message = ("Not a valid heading estimator type");
      return true;
    }

    // Check whether a valid type was requested
    if (!isValidType(desired_estimator)) {
      ROS_ERROR("[Odometry]: %d is not a valid heading estimator type", desired_estimator.type);
      res.success = false;
      res.message = ("Not a valid heading estimator type");
      return true;
    }

    desired_estimator.name = _heading_estimators_names[desired_estimator.type];

    bool success = false;
    {
      std::scoped_lock lock(mutex_hdg_estimator_type);

      success = changeCurrentHeadingEstimator(desired_estimator);
    }

    ROS_INFO("[Odometry]: %s", printOdometryDiag().c_str());

    res.success = success;
    res.message = (printOdometryDiag().c_str());

    return true;
  }  // namespace mrs_odometry

  //}

  /* //{ callbackToggleTeraranger() */

  bool Odometry::callbackToggleTeraranger(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res) {

    if (!is_initialized)
      return false;

    teraranger_enabled = req.data;

    res.success = true;
    res.message = (teraranger_enabled ? "Teraranger enabled" : "Teraranger disabled");

    if (teraranger_enabled) {

      ROS_INFO("[Odometry]: Teraranger enabled.");
      rtk_altitude_enabled = false;

    } else {

      ROS_INFO("[Odometry]: Teraranger disabled");
    }

    return true;
  }

  //}

  /* //{ callbackToggleGarmin() */

  bool Odometry::callbackToggleGarmin(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res) {

    if (!is_initialized)
      return false;

    garmin_enabled = req.data;

    res.success = true;
    res.message = (garmin_enabled ? "Garmin enabled" : "Garmin disabled");

    if (garmin_enabled) {

      ROS_INFO("[Odometry]: Garmin enabled.");
      rtk_altitude_enabled = false;

    } else {

      ROS_INFO("[Odometry]: Garmin disabled");
    }

    return true;
  }

  //}

  /* //{ callbackResetEstimator() */

  bool Odometry::callbackResetEstimator([[maybe_unused]] std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res) {

    if (!is_initialized)
      return false;

    Eigen::MatrixXd states  = Eigen::MatrixXd::Zero(lateral_n, 2);
    bool            success = false;

    // reset lateral kalman x
    {
      std::scoped_lock lock(mutex_current_estimator);
      success = current_estimator->getStates(states);
    }

    if (use_utm_origin_) {

      states(0, 0) = odom_pixhawk_shifted.pose.pose.position.x;
      states(0, 1) = odom_pixhawk_shifted.pose.pose.position.y;

    } else if (use_local_origin_) {
      // TODO there is a bug: when taking off, the position is set to local_origin instead of current pixhawk odom
      if (!land_position_set) {  // if taking off for the first time

        if (_estimator_type.type == mrs_msgs::EstimatorType::GPS || _estimator_type.type == mrs_msgs::EstimatorType::OPTFLOWGPS ||
            _estimator_type.type == mrs_msgs::EstimatorType::RTK) {
          if (!calculatePixhawkOdomOffset()) {
            ROS_ERROR("Calculating pixhawk odom offset failed");
          }
          states(0, 0) = odom_pixhawk_shifted.pose.pose.position.x;
          states(0, 1) = odom_pixhawk_shifted.pose.pose.position.y;
          ROS_INFO("[Odometry]: Resetting estimators to pijhawk shifted odom x: %f y: %f", states(0, 0), states(0, 1));
        } else {
          states(0, 0) = local_origin_x_;
          states(0, 1) = local_origin_y_;
          ROS_INFO("[Odometry]: Resetting estimators to local_origin x: %f y: %f", states(0, 0), states(0, 1));
        }

      } else if (use_local_origin_) {  // taking off again
        if (_estimator_type.type == mrs_msgs::EstimatorType::GPS || _estimator_type.type == mrs_msgs::EstimatorType::OPTFLOWGPS ||
            _estimator_type.type == mrs_msgs::EstimatorType::RTK) {
          if (!calculatePixhawkOdomOffset()) {
            ROS_ERROR("Calculating pixhawk odom offset failed");
          }
          states(0, 0) = odom_pixhawk_shifted.pose.pose.position.x;
          states(0, 1) = odom_pixhawk_shifted.pose.pose.position.y;
          ROS_INFO("[Odometry]: Resetting estimators to pixhawk shifted odom x: %f y: %f", states(0, 0), states(0, 1));
        } else {
          states(0, 0) = land_position_x;
          states(0, 1) = land_position_y;
          ROS_INFO("[Odometry]: Resetting estimators to land position x: %f y: %f", states(0, 0), states(0, 1));
        }
      }
    }

    if (!success) {

      ROS_ERROR("[Odometry]: Lateral kalman states and covariance reset failed #1.");

      res.success = false;
      res.message = "Reset of lateral kalman failed";

      return true;
    }

    states(1, 0) = 0.0;
    states(2, 0) = 0.0;
    states(3, 0) = 0.0;
    states(4, 0) = 0.0;
    states(5, 0) = 0.0;
    states(1, 1) = 0.0;
    states(2, 1) = 0.0;
    states(3, 1) = 0.0;
    states(4, 1) = 0.0;
    states(5, 1) = 0.0;

    {
      std::scoped_lock lock(mutex_current_estimator);
      success = current_estimator->reset(states);
    }

    if (!success) {

      ROS_ERROR("[Odometry]: Lateral kalman states and covariance reset failed #2.");

      res.success = false;
      res.message = "Reset of lateral kalman failed";

      return true;
    }

    ROS_WARN("[Odometry]: Lateral kalman states and covariance reset.");

    res.success = true;
    res.message = "Reset of lateral kalman successful";

    return true;
  }
  //}

  /* //{ callbackReconfigure() */
  void Odometry::callbackReconfigure([[maybe_unused]] mrs_odometry::lkfConfig &config, [[maybe_unused]] uint32_t level) {

    if (!is_initialized)
      return;
    ROS_INFO(
        "Reconfigure Request: "
        "Q_pos_mavros: %f, Q_pos_vio: %f, Q_pos_icp: %f, Q_pos_rtk: %f\nQ_vel_mavros: %f, Q_vel_vio: %f, Q_vel_icp: %f, Q_vel_optflow: "
        "%f, Q_vel_rtk: %f\nQ_tilt:%f\nR_pos: %f, R_vel: "
        "%f, R_acc: %f, R_acc_i: %f, R_acc_d: %f, R_tilt: %f",
        config.Q_pos_mavros, config.Q_pos_vio, config.Q_pos_icp, config.Q_pos_rtk, config.Q_vel_mavros, config.Q_vel_vio, config.Q_vel_icp,
        config.Q_vel_optflow, config.Q_vel_rtk, config.Q_tilt, config.R_pos, config.R_vel, config.R_acc, config.R_acc_i, config.R_acc_d, config.R_tilt);

    for (auto &estimator : m_state_estimators) {
      estimator.second->setQ(config.Q_pos_mavros, map_measurement_name_id.find("pos_mavros")->second);
      estimator.second->setQ(config.Q_pos_vio, map_measurement_name_id.find("pos_vio")->second);
      estimator.second->setQ(config.Q_pos_icp, map_measurement_name_id.find("pos_icp")->second);
      estimator.second->setQ(config.Q_pos_rtk, map_measurement_name_id.find("pos_rtk")->second);
      estimator.second->setQ(config.Q_vel_mavros, map_measurement_name_id.find("vel_mavros")->second);
      estimator.second->setQ(config.Q_vel_vio, map_measurement_name_id.find("vel_vio")->second);
      estimator.second->setQ(config.Q_vel_icp, map_measurement_name_id.find("vel_icp")->second);
      estimator.second->setQ(config.Q_vel_optflow * 1000, map_measurement_name_id.find("vel_optflow")->second);
      estimator.second->setQ(config.Q_vel_rtk, map_measurement_name_id.find("vel_rtk")->second);
      estimator.second->setQ(config.Q_tilt, map_measurement_name_id.find("tilt_mavros")->second);
    }

    for (auto &estimator : m_altitude_estimators) {
      estimator.second->setQ(config.Q_alt_baro, map_alt_measurement_name_id.find("alt_baro")->second);
      estimator.second->setQ(config.Q_z_vel_mavros, map_alt_measurement_name_id.find("vel_mavros")->second);
      estimator.second->setQ(config.Q_height_range, map_alt_measurement_name_id.find("height_range")->second);
      estimator.second->setQ(config.Q_bias_baro, map_alt_measurement_name_id.find("bias_baro")->second);
      estimator.second->setQ(config.Q_elevation, map_alt_measurement_name_id.find("elevation")->second);
    }
  }
  //}

  // --------------------------------------------------------------
  // |                      helper functions                      |
  // --------------------------------------------------------------

  /*  //{ stateEstimatorsPrediction() */

  void Odometry::stateEstimatorsPrediction(double x, double y, double dt) {

    if (!std::isfinite(x)) {
      ROS_ERROR("NaN detected in variable \"x\" (stateEstimatorsPrediction) !!!");
      return;
    }

    if (!std::isfinite(y)) {
      ROS_ERROR("NaN detected in variable \"y\" (stateEstimatorsPrediction) !!!");
      return;
    }

    Eigen::VectorXd input = Eigen::VectorXd::Zero(2);
    input << x, y;

    for (auto &estimator : m_state_estimators) {
      estimator.second->doPrediction(input, dt);
      /* Eigen::VectorXd pos_vec(2); */
      /* m_state_estimators[i]->getState(0, pos_vec); */
      /* ROS_INFO("[Odometry]: %s after prediction with input: %f, dt: %f x: %f", m_state_estimators[i]->getName().c_str(), input(0), dt, pos_vec(0)); */
    }
  }

  //}

  /*  //{ stateEstimatorsCorrection() */

  void Odometry::stateEstimatorsCorrection(double x, double y, const std::string &measurement_name) {

    std::map<std::string, int>::iterator it_measurement_id = map_measurement_name_id.find(measurement_name);
    if (it_measurement_id == map_measurement_name_id.end()) {
      ROS_ERROR("[Odometry]: Tried to fuse measurement with invalid name: \'%s\'.", measurement_name.c_str());
      return;
    }

    if (!std::isfinite(x)) {
      ROS_ERROR("NaN detected in variable \"x\" (stateEstimatorsCorrection) !!!");
      return;
    }

    if (!std::isfinite(y)) {
      ROS_ERROR("NaN detected in variable \"y\" (stateEstimatorsCorrection) !!!");
      return;
    }

    Eigen::VectorXd mes = Eigen::VectorXd::Zero(2);
    mes << x, y;

    for (auto &estimator : m_state_estimators) {
      estimator.second->doCorrection(mes, it_measurement_id->second);
      /* Eigen::VectorXd pos_vec(2); */
      /* estimator.second->getState(0, pos_vec); */
      /* ROS_INFO("[Odometry]: %s after %s correction: %f, x: %f", estimator.second->getName().c_str(), measurement_name.c_str(), mes(0), pos_vec(0)); */
    }
  }

  //}

  /*  //{ altitudeEstimatorCorrection() */

  void Odometry::altitudeEstimatorCorrection(double value, const std::string &measurement_name) {

    std::map<std::string, int>::iterator it_measurement_id = map_alt_measurement_name_id.find(measurement_name);
    if (it_measurement_id == map_alt_measurement_name_id.end()) {
      ROS_ERROR("[Odometry]: Tried to fuse measurement with invalid name: \'%s\'.", measurement_name.c_str());
      return;
    }

    if (!std::isfinite(value)) {
      ROS_ERROR("NaN detected in variable \"value\" (altitudeEstimatorCorrection) !!!");
      return;
    }

    if (fabs(value) > 100) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: Altitude estimator correction: %f", value);
    }

    Eigen::VectorXd mes = Eigen::VectorXd::Zero(1);
    mes << value;


    for (auto &estimator : m_altitude_estimators) {
      /* ROS_INFO_THROTTLE(1.0, "[Odometry]: estimator name: %s", estimator.second->getName().c_str()); */
      estimator.second->doCorrection(mes, it_measurement_id->second);
    }
  }

  //}

  /*  //{ altitudeEstimatorCorrection() */

  void Odometry::altitudeEstimatorCorrection(double value, const std::string &measurement_name,
                                             const std::shared_ptr<mrs_odometry::AltitudeEstimator> &estimator) {

    std::map<std::string, int>::iterator it_measurement_id = map_alt_measurement_name_id.find(measurement_name);
    if (it_measurement_id == map_alt_measurement_name_id.end()) {
      ROS_ERROR("[Odometry]: Tried to fuse measurement with invalid name: \'%s\'.", measurement_name.c_str());
      return;
    }

    if (!std::isfinite(value)) {
      ROS_ERROR("NaN detected in variable \"value\" (altitudeEstimatorCorrection) !!!");
      return;
    }

    Eigen::VectorXd mes = Eigen::VectorXd::Zero(1);
    mes << value;

    estimator->doCorrection(mes, it_measurement_id->second);
  }

  //}

  /*  //{ headingEstimatorsPrediction() */

  void Odometry::headingEstimatorsPrediction(const double yaw, const double yaw_rate, const double dt) {

    if (!std::isfinite(yaw)) {
      ROS_ERROR("NaN detected in variable \"yaw\" (headingEstimatorsPrediction) !!!");
      return;
    }

    if (!std::isfinite(yaw_rate)) {
      ROS_ERROR("NaN detected in variable \"yaw rate\" (headingEstimatorsPrediction) !!!");
      return;
    }


    for (auto &estimator : m_heading_estimators) {

      Eigen::VectorXd input = Eigen::VectorXd::Zero(2);
      input << yaw, yaw_rate;

      Eigen::VectorXd current_yaw = Eigen::VectorXd::Zero(1);
      estimator.second->getState(0, current_yaw);
      /* ROS_INFO("[Odometry]: Unwrapping %f with state %f", input(0), current_yaw(0)); */
      input(0) = unwrap(input(0), current_yaw(0));
      /* ROS_INFO("[Odometry]: After unwrap: %f", input(0)); */
      if (std::strcmp(estimator.second->getName().c_str(), "COMPASS") == STRING_EQUAL) {
        /* ROS_INFO("[Odometry]: %s: input: %f state: %f", estimator.second->getName().c_str(), input(0), current_yaw(0)); */
      }
      estimator.second->doPrediction(input, dt);
      Eigen::VectorXd yaw_state(1);
      Eigen::VectorXd yaw_rate_state(1);
      estimator.second->getState(0, yaw_state);
      estimator.second->getState(1, yaw_rate_state);
      /* ROS_INFO("[Odometry]: %s after prediction with input: %f,%f dt: %f state: %f,%f", estimator.second->getName().c_str(), input(0), input(1), dt,
       * yaw_state(0), yaw_rate_state(0)); */
    }
  }

  //}

  /*  //{ headingEstimatorsCorrection() */

  void Odometry::headingEstimatorsCorrection(const double value, const std::string &measurement_name) {

    /* ROS_INFO("[Odometry]: headingEstimatorCorrection(%f, %s)", value, measurement_name.c_str() ); */
    std::map<std::string, int>::iterator it_measurement_id = map_hdg_measurement_name_id.find(measurement_name);
    if (it_measurement_id == map_hdg_measurement_name_id.end()) {
      ROS_ERROR("[Odometry]: Tried to fuse measurement with invalid name: \'%s\'.", measurement_name.c_str());
      return;
    }

    if (!std::isfinite(value)) {
      ROS_ERROR("NaN detected in variable \"value\" (headingEstimatorsCorrection) !!!");
      return;
    }


    for (auto &estimator : m_heading_estimators) {
      /* ROS_INFO_THROTTLE(1.0, "[Odometry]: estimator name: %s", estimator.second->getName().c_str()); */

      Eigen::VectorXd mes = Eigen::VectorXd::Zero(1);
      mes << value;

      if (std::strcmp(measurement_name.c_str(), "yaw_compass") == STRING_EQUAL) {
        Eigen::VectorXd current_yaw = Eigen::VectorXd::Zero(1);
        estimator.second->getState(0, current_yaw);

        mes(0) = unwrap(mes(0), current_yaw(0));
        if (std::strcmp(estimator.second->getName().c_str(), "COMPASS") == STRING_EQUAL) {
          /* ROS_INFO("[Odometry]: %s: measurement: %f state: %f", estimator.second->getName().c_str(), mes(0), current_yaw(0)); */
        }
      }

      if (estimator.second->doCorrection(mes, it_measurement_id->second)) {
        Eigen::VectorXd yaw_state(1);
        Eigen::VectorXd yaw_rate_state(1);
        estimator.second->getState(0, yaw_state);
        estimator.second->getState(1, yaw_rate_state);
        /* ROS_INFO("[Odometry]: %s after correction with measurement: %f - %s, state: %f,%f", estimator.second->getName().c_str(), mes(0),
         * it_measurement_id->first.c_str(), yaw_state(0), yaw_rate_state(0)); */
      }
    }
  }

  //}

  /* //{ getGlobalRot() */
  void Odometry::getGlobalRot(const geometry_msgs::Quaternion &q_msg, double &rx, double &ry, double &rz) {

    tf2::Quaternion q_orig;
    tf2::fromMsg(q_msg, q_orig);
    q_orig.normalize();

    // Get roll, pitch, yaw in body frame
    double r, p, y, r_new, p_new;
    tf2::Matrix3x3(q_orig).getRPY(r, p, y);

    p_new = p * cos(-y) - r * sin(-y);
    r_new = r * cos(-y) + p * sin(-y);

    rx = r_new;
    ry = p_new;
    rz = y;
  }
  //}

  /* //{ changeCurrentEstimator() */
  bool Odometry::changeCurrentEstimator(const mrs_msgs::EstimatorType &desired_estimator) {

    mrs_msgs::EstimatorType target_estimator = desired_estimator;
    target_estimator.name                    = _estimator_type_names[target_estimator.type];

    Eigen::MatrixXd current_altitude = Eigen::MatrixXd::Zero(altitude_n, 1);
    {
      std::scoped_lock lock(mutex_altitude_estimator);
      if (!current_alt_estimator->getStates(current_altitude)) {
        ROS_WARN("[Odometry]: Altitude estimator not initialized.");
        return false;
      }
    }

    // Optic flow type
    if (target_estimator.type == mrs_msgs::EstimatorType::OPTFLOW) {

      if (!_optflow_available) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOW type. Optic flow not available in this world.");
        return false;
      }

      if (!got_optflow && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOW type. No new optic flow msgs received.");
        return false;
      }

      if (current_altitude(2) > _max_optflow_altitude) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOW type. Current altitude %f. Must descend to %f.", current_altitude(2), _max_optflow_altitude);
        return false;
      }

      max_altitude = _max_optflow_altitude;
      ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);

      // Mavros GPS type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::GPS) {

      if (!_gps_available) {
        ROS_ERROR("[Odometry]: Cannot transition to GPS type. GPS signal not available in this world.");
        return false;
      }

      if (!gps_reliable) {
        ROS_ERROR("[Odometry]: Cannot transition to GPS type. Not enough satellites: %d. Required %d.", mavros_diag.gps.satellites_visible, _min_satellites);
        return false;
      }

      max_altitude = _max_default_altitude;
      ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);

      // Optic flow + Mavros GPS type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::OPTFLOWGPS) {

      if (!_optflow_available) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOWGPS type. Optic flow not available in this world.");
        return false;
      }

      if (!got_optflow && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOWGPS type. No new optic flow msgs received.");
        return false;
      }

      if (current_altitude(2) > _max_optflow_altitude) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOWGPS type. Current altitude %f. Must descend to %f.", current_altitude(2), _max_optflow_altitude);
        return false;
      }

      if (!_gps_available) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOWGPS type. GPS signal not available in this world.");
        return false;
      }

      if (!gps_reliable) {
        ROS_ERROR("[Odometry]: Cannot transition to OPTFLOWGPS type. Not enough satellites: %d. Required %d.", mavros_diag.gps.satellites_visible,
                  _min_satellites);
        return false;
      }

      max_altitude = _max_default_altitude;
      ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);

      // RTK GPS type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::RTK) {

      if (!_rtk_available) {
        ROS_ERROR("[Odometry]: Cannot transition to RTK type. RTK signal not available in this world.");
        return false;
      }

      if (!got_rtk && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to RTK type. No new rtk msgs received.");
        return false;
      }

      if (!_gps_available) {
        ROS_ERROR("[Odometry]: Cannot transition to RTK type. GPS signal not available in this world.");
        return false;
      }

      if (!gps_reliable) {
        ROS_ERROR("[Odometry]: Cannot transition to RTK type. Not enough satellites: %d. Required %d.", mavros_diag.gps.satellites_visible, _min_satellites);
        return false;
      }

      max_altitude = _max_default_altitude;
      ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);

      // T265 type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::T265) {

      if (!_t265_available) {
        ROS_ERROR("[Odometry]: Cannot transition to T265 type. T265 not available in this world.");
        return false;
      }

      if (!got_odom_t265 && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to T265 type. No new T265 odom msgs received.");
        return false;
      }

      if (!t265_reliable) {
        ROS_ERROR("[Odometry]: Cannot transition to T265 type. T265 odometry is not reliable.");
        return false;
      }

      max_altitude = _max_default_altitude;
      ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);

      // LIDAR localization type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::ICP) {

      if (!_lidar_available) {
        ROS_ERROR("[Odometry]: Cannot transition to ICP type. Lidar localization not available in this world.");
        return false;
      }

      if (!got_icp && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to ICP type. No new icp msgs received.");
        return false;
      }

      // Vio localization type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::VIO) {

      if (!_vio_available) {
        ROS_ERROR("[Odometry]: Cannot transition to VIO type. Visual odometry not available in this world.");
        return false;
      }

      if (!got_vio && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to VIO type. No new vio msgs received.");
        return false;
      }

      if (!_gps_available) {
        max_altitude = _max_optflow_altitude;
        ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);
      } else {
        max_altitude = _max_default_altitude;
        ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);
      }

      // Object localization type
    } else if (target_estimator.type == mrs_msgs::EstimatorType::OBJECT) {

      if (!_object_available) {
        ROS_ERROR("[Odometry]: Cannot transition to OBJECT type. Object odometry not available in this world.");
        return false;
      }

      if (!got_object && is_ready_to_takeoff) {
        ROS_ERROR("[Odometry]: Cannot transition to OBJECT type. No new object msgs received.");
        return false;
      }

      if (!object_reliable) {
        ROS_ERROR("[Odometry]: Cannot transition to OBJECT type. Object detection is not reliable");
        return false;
      }

      fallback_object_estimator_type = _estimator_type;

      if (!_gps_available) {
        max_altitude = _max_optflow_altitude;
        ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);
      } else {
        max_altitude = _max_default_altitude;
        ROS_WARN("[Odometry]: Setting max_altitude to %f", max_altitude);
      }

    } else {

      ROS_ERROR("[Odometry]: Rejected transition to invalid type %s.", target_estimator.name.c_str());
      return false;
    }

    if (stringInVector(target_estimator.name, _state_estimators_names)) {

      {
        std::scoped_lock lock(mutex_current_estimator);

        /* ROS_WARN_STREAM("[Odometry]: " << m_state_estimators.find(target_estimator.name)->second->getName()); */
        current_estimator      = m_state_estimators.find(target_estimator.name)->second;
        current_estimator_name = current_estimator->getName();
      }

      ROS_WARN("[Odometry]: Transition to %s state estimator successful", current_estimator_name.c_str());

    } else {
      ROS_WARN("[Odometry]: Requested transition to nonexistent state estimator %s", target_estimator.name.c_str());
      return false;
    }

    _estimator_type      = target_estimator;
    _estimator_type.name = _estimator_type_names[_estimator_type.type];
    return true;
  }

  //}

  /* //{ changeCurrentAltitudeEstimator() */
  bool Odometry::changeCurrentAltitudeEstimator(const mrs_msgs::AltitudeType &desired_estimator) {

    mrs_msgs::AltitudeType target_estimator = desired_estimator;
    target_estimator.name                   = _altitude_type_names[target_estimator.type];

    if (target_estimator.type != mrs_msgs::AltitudeType::HEIGHT && target_estimator.type != mrs_msgs::AltitudeType::ALTITUDE &&
        target_estimator.type != mrs_msgs::AltitudeType::ELEVATION) {
      ROS_ERROR("[Odometry]: Rejected transition to invalid type %s.", target_estimator.name.c_str());
      return false;
    }

    if (stringInVector(target_estimator.name, _altitude_estimators_names)) {
      {
        std::scoped_lock lock(mutex_current_alt_estimator);

        /* ROS_WARN_STREAM("[Odometry]: " << m_state_estimators.find(target_estimator.name)->second->getName()); */
        current_alt_estimator      = m_altitude_estimators.find(target_estimator.name)->second;
        current_alt_estimator_name = current_alt_estimator->getName();
      }

      ROS_WARN("[Odometry]: Transition to %s altitude estimator successful", current_alt_estimator_name.c_str());

    } else {
      ROS_WARN("[Odometry]: Requested transition to nonexistent altitude estimator %s", target_estimator.name.c_str());
      return false;
    }

    _alt_estimator_type      = target_estimator;
    _alt_estimator_type.name = _altitude_type_names[_alt_estimator_type.type];
    return true;
  }

  //}

  /* //{ changeCurrentHeadingEstimator() */
  bool Odometry::changeCurrentHeadingEstimator(const mrs_msgs::HeadingType &desired_estimator) {

    mrs_msgs::HeadingType target_estimator = desired_estimator;
    target_estimator.name                  = _heading_type_names[target_estimator.type];

    if (target_estimator.type != mrs_msgs::HeadingType::GYRO && target_estimator.type != mrs_msgs::HeadingType::COMPASS &&
        target_estimator.type != mrs_msgs::HeadingType::OPTFLOW) {
      ROS_ERROR("[Odometry]: Rejected transition to invalid type %s.", target_estimator.name.c_str());
      return false;
    }

    if (stringInVector(target_estimator.name, _heading_estimators_names)) {
      {
        std::scoped_lock lock(mutex_current_hdg_estimator);

        /* ROS_WARN_STREAM("[Odometry]: " << m_state_estimators.find(target_estimator.name)->second->getName()); */
        current_hdg_estimator      = m_heading_estimators.find(target_estimator.name)->second;
        current_hdg_estimator_name = current_hdg_estimator->getName();
      }

      ROS_WARN("[Odometry]: Transition to %s heading estimator successful", current_hdg_estimator_name.c_str());

    } else {
      ROS_WARN("[Odometry]: Requested transition to nonexistent heading estimator %s", target_estimator.name.c_str());
      return false;
    }

    _hdg_estimator_type      = target_estimator;
    _hdg_estimator_type.name = _heading_type_names[_hdg_estimator_type.type];
    return true;
  }

  //}

  /* //{ isValidType() */
  bool Odometry::isValidType(const mrs_msgs::EstimatorType &type) {

    if (type.type == mrs_msgs::EstimatorType::OPTFLOW || type.type == mrs_msgs::EstimatorType::GPS || type.type == mrs_msgs::EstimatorType::OPTFLOWGPS ||
        type.type == mrs_msgs::EstimatorType::RTK || type.type == mrs_msgs::EstimatorType::ICP || type.type == mrs_msgs::EstimatorType::VIO ||
        type.type == mrs_msgs::EstimatorType::OBJECT || type.type == mrs_msgs::EstimatorType::T265) {
      return true;
    }

    return false;
  }

  //}

  /* //{ isValidType() */
  bool Odometry::isValidType(const mrs_msgs::HeadingType &type) {

    if (type.type == mrs_msgs::HeadingType::GYRO || type.type == mrs_msgs::HeadingType::COMPASS || type.type == mrs_msgs::HeadingType::OPTFLOW) {
      return true;
    }

    return false;
  }

  //}

  /* isTimestampOK() //{ */
  bool Odometry::isTimestampOK(const double curr_sec, const double prev_sec) {

    double delta_tol = 100;

    double delta = curr_sec - prev_sec;

    if (curr_sec < 0.0) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: current timestamp negative: %f", curr_sec);
      return false;
    }

    if (prev_sec < 0.0) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: current timestamp negative: %f", prev_sec);
      return false;
    }

    if (delta < 0.0) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: time delta negative: %f", delta);
      return false;
    }

    if (fabs(delta) < 0.001) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: time delta too small: %f", delta);
      return false;
    }

    if (delta > delta_tol) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: time delta %f > %f", delta, delta_tol);
      return false;
    }

    return true;
  }
  //}

  /* //{ printOdometryDiag() */
  std::string Odometry::printOdometryDiag() {

    std::string s_diag;

    // lateral
    mrs_msgs::EstimatorType type;

    {
      std::scoped_lock lock(mutex_estimator_type);

      type.type = _estimator_type.type;
    }

    s_diag += "Current lateral estimator type: ";
    s_diag += std::to_string(type.type);
    s_diag += " - ";

    if (type.type == mrs_msgs::EstimatorType::OPTFLOW) {
      s_diag += "OPTFLOW";
    } else if (type.type == mrs_msgs::EstimatorType::GPS) {
      s_diag += "GPS";
    } else if (type.type == mrs_msgs::EstimatorType::OPTFLOWGPS) {
      s_diag += "OPTFLOWGPS";
    } else if (type.type == mrs_msgs::EstimatorType::RTK) {
      s_diag += "RTK";
    } else if (type.type == mrs_msgs::EstimatorType::ICP) {
      s_diag += "ICP";
    } else if (type.type == mrs_msgs::EstimatorType::VIO) {
      s_diag += "VIO";
    } else if (type.type == mrs_msgs::EstimatorType::OBJECT) {
      s_diag += "OBJECT";
    } else if (type.type == mrs_msgs::EstimatorType::T265) {
      s_diag += "T265";
    } else {
      s_diag += "UNKNOWN";
    }

    // heading
    mrs_msgs::HeadingType hdg_type;

    {
      std::scoped_lock lock(mutex_hdg_estimator_type);

      hdg_type.type = _hdg_estimator_type.type;
    }
    s_diag += ", Current heading estimator type: ";
    s_diag += std::to_string(type.type);
    s_diag += " - ";

    if (hdg_type.type == mrs_msgs::HeadingType::GYRO) {
      s_diag += "GYRO";
    } else if (hdg_type.type == mrs_msgs::HeadingType::COMPASS) {
      s_diag += "COMPASS";
    } else if (hdg_type.type == mrs_msgs::HeadingType::OPTFLOW) {
      s_diag += "OPTFLOW";
    } else {
      s_diag += "UNKNOWN";
    }
    return s_diag;
  }

  //}

  /* stringInVector() //{ */

  bool Odometry::stringInVector(const std::string &value, const std::vector<std::string> &vector) {

    if (std::find(vector.begin(), vector.end(), value) == vector.end()) {
      return false;
    } else {
      return true;
    }
  }

  //}

  /* calculatePixhawkOdomOffset() //{ */

  bool Odometry::calculatePixhawkOdomOffset(void) {

    /* if (!got_home_position_fix && */
    /*     (_estimator_type.type == mrs_msgs::EstimatorType::RTK || _estimator_type.type == mrs_msgs::EstimatorType::GPS || */
    /*      _estimator_type.type == mrs_msgs::EstimatorType::OPTFLOWGPS || (_estimator_type.type == mrs_msgs::EstimatorType::OPTFLOW && gps_reliable))) { */

    if ((use_utm_origin_ && !got_pixhawk_utm) || !got_odom_pixhawk) {
      ROS_WARN_THROTTLE(1.0, "[Odometry]: cannot calculate pixhawk_odom_offset, waiting for data: UTM: %s, ODOM: %s", btoa(got_pixhawk_utm),
                        btoa(got_odom_pixhawk));
      return false;
    }

    if (got_pixhawk_odom_offset) {
      return true;
    }

    // when we have defined our home position, set local origin offset
    if (use_utm_origin_) {

      {
        std::scoped_lock lock(mutex_odom_pixhawk, mutex_pixhawk_utm_position);

        pixhawk_odom_offset_x = (pixhawk_utm_position_x - odom_pixhawk.pose.pose.position.x) - utm_origin_x_;
        pixhawk_odom_offset_y = (pixhawk_utm_position_y - odom_pixhawk.pose.pose.position.y) - utm_origin_y_;
      }

      ROS_INFO("[Odometry]: pixhawk_odom_offset based in local_utm calculated as: x: %f, y: %f", pixhawk_odom_offset_x, pixhawk_odom_offset_y);

      got_pixhawk_odom_offset = true;
      return true;

      // when we have not define our home position, define it as our averaged home position
    } else if (use_local_origin_) {

      {
        std::scoped_lock lock(mutex_odom_pixhawk);

        pixhawk_odom_offset_x = local_origin_x_ - odom_pixhawk.pose.pose.position.x;
        pixhawk_odom_offset_y = local_origin_y_ - odom_pixhawk.pose.pose.position.y;
      }

      ROS_INFO("[Odometry]: pixhawk_odom_offset based in local_origin calculated as: x: %f, y: %f", pixhawk_odom_offset_x, pixhawk_odom_offset_y);

      got_pixhawk_odom_offset = true;
      return true;

    } else {
      ROS_ERROR("[Odometry]: Cannot calculate pixhawk_odom_offset_y, TODO");
      ros::shutdown();
    }

    return false;
  }

  //}

  /* applyOdomOffset //{ */
  nav_msgs::Odometry Odometry::applyOdomOffset(const nav_msgs::Odometry &msg) {
    nav_msgs::Odometry ret = msg;

    tf2::Vector3 v;
    tf2::fromMsg(msg.pose.pose.position, v);
    v = v - m_pos_odom_offset;
    tf2::toMsg(v, ret.pose.pose.position);

    tf2::Quaternion q;
    tf2::fromMsg(msg.pose.pose.orientation, q);
    q = m_rot_odom_offset * q;
    tf2::toMsg(v, ret.pose.pose.position);

    return ret;
  }

  //}

  /* unwrap() //{ */
  double Odometry::unwrap(const double yaw, const double yaw_previous) {
    if (yaw - yaw_previous > M_PI) {
      return yaw - 2 * M_PI;
    } else if (yaw - yaw_previous < -M_PI) {
      return yaw + 2 * M_PI;
    }
    return yaw;
  }

  /* wrap() //{ */
  double Odometry::wrap(const double angle_in) {

    double angle_wrapped = angle_in;

    while (angle_wrapped > M_PI) {
      angle_wrapped -= 2 * M_PI;
    }

    while (angle_wrapped < -M_PI) {
      angle_wrapped += 2 * M_PI;
    }

    return angle_wrapped;
  }
  //}

  //}

  /* setYaw() //{ */
  void Odometry::setYaw(geometry_msgs::Quaternion &q_msg, const double yaw_in) {
    tf2::Quaternion q_tf;
    tf2::fromMsg(q_msg, q_tf);
    double roll, pitch, yaw;
    tf2::Matrix3x3(q_tf).getRPY(roll, pitch, yaw);
    yaw = yaw_in;
    q_tf.setRPY(roll, pitch, yaw);
    q_msg = tf2::toMsg(q_tf);

    q_tf.normalize();
  }
  //}

}  // namespace mrs_odometry

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(mrs_odometry::Odometry, nodelet::Nodelet)
