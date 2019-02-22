#include "CV_Main.h"

CV_Main::CV_Main()
    : m_camera_ready(false), m_image(nullptr), m_image_reader(nullptr), m_native_camera(nullptr), scan_mode(false) {

  // This issue is because OpenCV takes a file name but NDK AAssetManager only gives file descriptor
  if( !face_cascade.load( face_cascade_name ) ){ LOGE("--(!)Error loading face cascade\n"); };
  if( !eyes_cascade.load( eyes_cascade_name ) ){ LOGE("--(!)Error loading eyes cascade\n"); };
};

CV_Main::~CV_Main() {
  // clean up VM and callback handles
  JNIEnv* env;
  java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  env->DeleteGlobalRef(calling_activity_obj);
  calling_activity_obj = nullptr;

  // ACameraCaptureSession_stopRepeating(m_capture_session);
  if (m_native_camera != nullptr) {
    delete m_native_camera;
    m_native_camera = nullptr;
  }

  // make sure we don't leak native windows
  if (m_native_window != nullptr) {
    ANativeWindow_release(m_native_window);
    m_native_window = nullptr;
  }

  if (m_image_reader != nullptr) {
    delete (m_image_reader);
    m_image_reader = nullptr;
  }
}

void CV_Main::OnCreate(JNIEnv* env, jobject caller_activity) {
  // Need to create an instance of the Java activity
  calling_activity_obj = env->NewGlobalRef(caller_activity);

  // Need to enter package and class to find Java class
  jclass handler_class = env->GetObjectClass(caller_activity);

  // Create function pointeACameraManager_getCameraCharacteristicsr to use for
  // on_loaded callbacks
  // on_callback = env->GetMethodID(handler_class, "JAVA_FUNCTION", "()V");
}

void CV_Main::OnPause() {}

void CV_Main::OnDestroy() {}

void CV_Main::SetNativeWindow(ANativeWindow* native_window) {
  // Save native window
  m_native_window = native_window;
}

void CV_Main::SetUpCamera() {

  m_native_camera = new Native_Camera(m_selected_camera_type);

  m_native_camera->MatchCaptureSizeRequest(&m_view,
                                           ANativeWindow_getWidth(m_native_window),
                                           ANativeWindow_getHeight(m_native_window));

  ASSERT(m_view.width && m_view.height, "Could not find supportable resolution");

  // Here we set the buffer to use RGBX_8888 as default might be; RGB_565
  ANativeWindow_setBuffersGeometry(m_native_window, m_view.height, m_view.width,
                                   WINDOW_FORMAT_RGBX_8888);

  m_image_reader = new Image_Reader(&m_view, AIMAGE_FORMAT_YUV_420_888);
  m_image_reader->SetPresentRotation(m_native_camera->GetOrientation());

  ANativeWindow* image_reader_window = m_image_reader->GetNativeWindow();

  m_camera_ready = m_native_camera->CreateCaptureSession(image_reader_window);
}

void CV_Main::CameraLoop() {
  bool buffer_printout = false;

  while (1) {
    if (m_camera_thread_stopped) { break; }
    if (!m_camera_ready || !m_image_reader) { continue; }
    m_image = m_image_reader->GetLatestImage();
    if (m_image == nullptr) { continue; }

    ANativeWindow_acquire(m_native_window);
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(m_native_window, &buffer, nullptr) < 0) {
      m_image_reader->DeleteImage(m_image);
      m_image = nullptr;
      continue;
    }

    if (false == buffer_printout) {
      buffer_printout = true;
      LOGI("/// H-W-S-F: %d, %d, %d, %d", buffer.height, buffer.width, buffer.stride, buffer.format);
    }

    m_image_reader->DisplayImage(&buffer, m_image);

    display_mat = cv::Mat(buffer.height, buffer.stride, CV_8UC4, buffer.bits);

    if (true == scan_mode) {
      FaceDetect(display_mat);
    }

    ANativeWindow_unlockAndPost(m_native_window);
    ANativeWindow_release(m_native_window);
  }
  FlipCamera();
}

void CV_Main::FaceDetect(cv::Mat &frame) {

  std::vector<cv::Rect> faces;
  cv::Mat frame_gray;

  cv::cvtColor(frame, frame_gray, CV_RGBA2GRAY);

 // equalizeHist( frame_gray, frame_gray );

 //-- Detect faces
 /*Commented to avoid gnu_stl dependency  error from precompiled opencv libs*/
/*  face_cascade.detectMultiScale(frame_gray, faces, 1.18, 2, 0|CV_HAAR_SCALE_IMAGE, cv::Size(70, 70));*/

  for( size_t i = 0; i < faces.size(); i++ ) {
    cv::Point center(faces[i].x + faces[i].width*0.5, faces[i].y + faces[i].height*0.5);

    ellipse(frame, center, cv::Size( faces[i].width*0.5, faces[i].height*0.5), 0, 0, 360,
            CV_PURPLE, 4, 8, 0);

    cv::Mat faceROI = frame_gray( faces[i] );
    std::vector<cv::Rect> eyes;

    //-- In each face, detect eyes
      /*Commented to avoid gnu_stl dependency  error from precompiled opencv libs*/
/*    eyes_cascade.detectMultiScale( faceROI, eyes, 1.2, 2, 0 |CV_HAAR_SCALE_IMAGE, cv::Size(45, 45) );*/

    for( size_t j = 0; j < eyes.size(); j++ ) {
      cv::Point center( faces[i].x + eyes[j].x + eyes[j].width*0.5, faces[i].y + eyes[j].y + eyes[j].height*0.5 );
      int radius = cvRound( (eyes[j].width + eyes[j].height)*0.25 );
      circle( frame, center, radius, CV_RED, 4, 8, 0 );
    }
  }

  end_t = clock();
  total_t += (double)(end_t - start_t) / CLOCKS_PER_SEC;
  LOGI("Current Time: %f", total_t);
  if (total_t >= 20) {
    // stop after 20 seconds
    LOGI("DONE WITH 20 SECONDS");
    scan_mode = false;
  }
  start_t = clock();
}

// When scan button is hit
void CV_Main::RunCV() {
  scan_mode = true;
  total_t = 0;
  start_t = clock();
}

void CV_Main::HaltCamera() {
  if (m_native_camera == nullptr) {
    LOGE("Can't flip camera without camera instance");
    return; // need to setup camera
  } else if (m_native_camera->GetCameraCount() < 2) {
    LOGE("Only one camera is available"); // TODO - remove button if this is true
    return; // need a second camera to flip with
  }

  m_camera_thread_stopped = true;
}

void CV_Main::FlipCamera() {
  m_camera_thread_stopped = false;

  // reset info
  if (m_image_reader != nullptr) {
    delete (m_image_reader);
    m_image_reader = nullptr;
  }
  delete m_native_camera;

  if (m_selected_camera_type == FRONT_CAMERA) {
    m_selected_camera_type = BACK_CAMERA;
  } else {
    m_selected_camera_type = FRONT_CAMERA;
  }

  SetUpCamera();

  std::thread loopThread(&CV_Main::CameraLoop, this);
  loopThread.detach();
}