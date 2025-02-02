/********************************************************************
 * Copyright (C) 2015 Liangliang Nan <liangliang.nan@gmail.com>
 * https://3d.bk.tudelft.nl/liangliang/
 *
 * This file is part of Easy3D. If it is useful in your research/work,
 * I would be grateful if you show your appreciation by citing it:
 * ------------------------------------------------------------------
 *      Liangliang Nan.
 *      Easy3D: a lightweight, easy-to-use, and efficient C++ library
 *      for processing and rendering 3D data.
 *      Journal of Open Source Software, 6(64), 3255, 2021.
 * ------------------------------------------------------------------
 *
 * Easy3D is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 3
 * as published by the Free Software Foundation.
 *
 * Easy3D is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************/

#include "viewer.h"

#include <QKeyEvent>
#include <QPainter>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObjectFormat>
#include <QApplication>

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/shader_program.h>
#include <easy3d/renderer/shader_manager.h>
#include <easy3d/renderer/shape.h>
#include <easy3d/renderer/transform.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/manipulated_camera_frame.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/renderer/opengl_util.h>
#include <easy3d/renderer/opengl_error.h>
#include <easy3d/renderer/texture_manager.h>
#include <easy3d/renderer/text_renderer.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/resource.h>
#include <easy3d/fileio/surface_mesh_io.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/setting.h>


namespace easy3d {

    // \cond

    Viewer::Viewer(QWidget *parent /* = nullptr*/)
            : QOpenGLWidget(parent), texter_(nullptr), pressed_button_(Qt::NoButton),
              mouse_pressed_pos_(0, 0), mouse_previous_pos_(0, 0), show_pivot_point_(false), drawable_axes_(nullptr),
              model_idx_(-1) {
        // like Qt::StrongFocus plus the widget accepts focus by using the mouse wheel.
        setFocusPolicy(Qt::WheelFocus);
        setMouseTracking(true);

        camera_ = new Camera;
        camera_->setType(Camera::PERSPECTIVE);
        camera_->setUpVector(vec3(0, 0, 1)); // Z pointing up
        camera_->setViewDirection(vec3(-1, 0, 0)); // X pointing out
        camera_->showEntireScene();

        easy3d::connect(&camera_->frame_modified, this, static_cast<void (Viewer::*)(void)>(&Viewer::update));
    }


    Viewer::~Viewer() {
        // Make sure the context is current and then explicitly
        // destroy all underlying OpenGL resources.
        makeCurrent();

        cleanup();

        doneCurrent();

        LOG(INFO) << "viewer terminated. Bye!";
    }


    void Viewer::cleanup() {
        delete camera_;
        delete drawable_axes_;
        delete texter_;

        for (auto m: models_)
            delete m;
		models_.clear();

		for (auto d : drawables_)
			delete d;
		drawables_.clear();

        ShaderManager::terminate();
        TextureManager::terminate();
    }


    void Viewer::init() {
        const std::string file_name = resource::directory() + "/data/easy3d.ply";
        auto mesh = SurfaceMeshIO::load(file_name);
        if (mesh)
            addModel(mesh);

        // We always want to look at the front of the easy3d logo.
        camera()->setViewDirection(vec3(0, 0, -1));
        camera()->setUpVector(vec3(0, 1, 0));
        fitScreen(mesh);
    }


    void Viewer::initializeGL() {
        QOpenGLWidget::initializeGL();
        initializeOpenGLFunctions();

        OpenglUtil::init();
#ifndef NDEBUG
        opengl::setup_gl_debug_callback();
#endif

        if (!hasOpenGLFeature(QOpenGLFunctions::Multisample))
            throw std::runtime_error("Multisample not supported on this machine!!! Viewer may not run properly");
        if (!hasOpenGLFeature(QOpenGLFunctions::Framebuffers))
            throw std::runtime_error(
                    "Framebuffer Object is not supported on this machine!!! Viewer may not run properly");

        background_color_ = setting::background_color;

        glEnable(GL_DEPTH_TEST);
        glClearDepthf(1.0f);
        glClearColor(background_color_[0], background_color_[1], background_color_[2], background_color_[3]);

        int major_requested = QSurfaceFormat::defaultFormat().majorVersion();
        int minor_requested = QSurfaceFormat::defaultFormat().minorVersion();
        VLOG(1) << "OpenGL vendor: " << glGetString(GL_VENDOR);
        VLOG(1) << "OpenGL renderer: " << glGetString(GL_RENDERER);
        VLOG(1) << "OpenGL version requested: " << major_requested << "." << minor_requested;
        VLOG(1) << "OpenGL version received: " << glGetString(GL_VERSION);
        VLOG(1) << "GLSL version received: " << glGetString(GL_SHADING_LANGUAGE_VERSION);

        int major = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        int minor = 0;
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major * 10 + minor < 32) {
            throw std::runtime_error("Viewer requires at least OpenGL 3.2");
        }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
        dpi_scaling_ = static_cast<float>(devicePixelRatioF());
#else
        dpi_scaling_ = static_cast<float>(devicePixelRatio());
#endif
        VLOG(1) << "DPI scaling: " << dpiScaling();

        // This won't work because QOpenGLWidget draws everything in framebuffer and
        // the framebuffer has not been created in the initializeGL() method. We
        // will query the actual samples in the paintGL() method.
        //int samples_received = 0;
        //func_->glgetintegerv(gl_samples, &samples_received);

        // create TextRenderer renderer and load default fonts
        texter_ = new TextRenderer(dpiScaling());
        texter_->add_font(resource::directory() + "/fonts/en_Earth-Normal.ttf");
        texter_->add_font(resource::directory() + "/fonts/en_Roboto-Medium.ttf");

        // Calls user defined method.
        init();

        // print usage
        std::cout << usage() << std::endl;

        timer_.start();
    }


    void Viewer::resizeGL(int w, int h) {
        QOpenGLWidget::resizeGL(w, h);

        // The viewport is set up by QOpenGLWidget before drawing.
        // So I don't need to set up.
        // func_->glViewport(0, 0, static_cast<int>(w * highdpi_), static_cast<int>(h * highdpi_));

        camera()->setScreenWidthAndHeight(w, h);
    }


    void Viewer::setBackgroundColor(const vec4 &c) {
        background_color_ = c;

        makeCurrent();
        glClearColor(background_color_[0], background_color_[1], background_color_[2], background_color_[3]);
        doneCurrent();
    }


    void Viewer::mousePressEvent(QMouseEvent *e) {
        pressed_button_ = e->button();
        mouse_previous_pos_ = e->pos();
        mouse_pressed_pos_ = e->pos();

        camera_->frame()->action_start();
        if (e->modifiers() == Qt::ShiftModifier) {
            if (e->button() == Qt::LeftButton) {
                bool found = false;
                const vec3 &p = pointUnderPixel(e->pos(), found);
                if (found) {
                    camera()->interpolateToLookAt(p);
                    camera_->setPivotPoint(p);

                    // show, but hide the visual hint of pivot point after \p delay milliseconds.
                    show_pivot_point_ = true;
                    Timer<>::single_shot(10000, [&]() {
                        show_pivot_point_ = false;
                        update();
                    });
                } else {
                    camera_->setPivotPoint(camera_->sceneCenter());
                    show_pivot_point_ = false;
                }
            } else if (e->button() == Qt::RightButton) {
                camera()->interpolateToFitScene();
                camera_->setPivotPoint(camera_->sceneCenter());
                show_pivot_point_ = false;
            }
        }

        QOpenGLWidget::mousePressEvent(e);
        update();
    }


    void Viewer::mouseReleaseEvent(QMouseEvent *e) {
        if (e->button() == Qt::LeftButton && e->modifiers() == Qt::ControlModifier) { // ZOOM_ON_REGION
            int xmin = std::min(mouse_pressed_pos_.x(), e->pos().x());
            int xmax = std::max(mouse_pressed_pos_.x(), e->pos().x());
            int ymin = std::min(mouse_pressed_pos_.y(), e->pos().y());
            int ymax = std::max(mouse_pressed_pos_.y(), e->pos().y());
            camera_->fitScreenRegion(xmin, ymin, xmax, ymax);
        } else
            camera_->frame()->action_end();

        pressed_button_ = Qt::NoButton;
        mouse_pressed_pos_ = QPoint(0, 0);

        QOpenGLWidget::mouseReleaseEvent(e);
        update();
    }


    void Viewer::mouseMoveEvent(QMouseEvent *e) {
        int x = e->pos().x(), y = e->pos().y();
        // Restrict the cursor to be within the client area during dragging
        if (x < 0 || x > width() || y < 0 || y > height()) {
            e->ignore();
            return;
        }

        if (pressed_button_ != Qt::NoButton) { // button pressed
            if (e->modifiers() == Qt::ControlModifier) {
                // zoom on region
            } else {
                int dx = x - mouse_previous_pos_.x();
                int dy = y - mouse_previous_pos_.y();
                if (pressed_button_ == Qt::LeftButton)
                    camera_->frame()->action_rotate(x, y, dx, dy, camera_, ManipulatedFrame::NONE);
                else if (pressed_button_ == Qt::RightButton)
                    camera_->frame()->action_translate(x, y, dx, dy, camera_, ManipulatedFrame::NONE);
                else if (pressed_button_ == Qt::MiddleButton) {
                    if (dy != 0)
                        camera_->frame()->action_zoom(dy > 0 ? 1 : -1, camera_);
                }
            }
        }

        mouse_previous_pos_ = e->pos();
        QOpenGLWidget::mouseMoveEvent(e);
    }


    void Viewer::mouseDoubleClickEvent(QMouseEvent *e) {
        QOpenGLWidget::mouseDoubleClickEvent(e);
        update();
    }


    void Viewer::wheelEvent(QWheelEvent *e) {
        const int delta = e->angleDelta().y();
        if (delta <= -1 || delta >= 1) {
            int dy = e->angleDelta().y() > 0 ? 1 : -1;
            camera_->frame()->action_zoom(dy, camera_);
        }

        QOpenGLWidget::wheelEvent(e);
        update();
    }


    bool Viewer::saveSnapshot(const QString &file_name) {
        makeCurrent();

        int w = static_cast<int>(static_cast<float>(width()) * dpiScaling());
        int h = static_cast<int>(static_cast<float>(height()) * dpiScaling());

        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        auto fbo = new QOpenGLFramebufferObject(w, h, format);
        fbo->addColorAttachment(w, h);

        fbo->bind();
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        draw();

        fbo->release();

        const QImage &image = fbo->toImage();

        // restore the clear color
        glClearColor(background_color_[0], background_color_[1], background_color_[2], background_color_[3]);

        doneCurrent();

        return image.save(file_name);
    }


    Model *Viewer::currentModel() const {
        if (models_.empty())
            return nullptr;
        if (model_idx_ < models_.size())
            return models_[model_idx_];
        return nullptr;
    }

    void Viewer::keyPressEvent(QKeyEvent *e) {
        if (e->key() == Qt::Key_F1 && e->modifiers() == Qt::NoModifier)
            std::cout << usage() << std::endl;
        else if (e->key() == Qt::Key_Left && e->modifiers() == Qt::KeypadModifier) {
            auto angle = static_cast<float>(1 * M_PI / 180.0); // turn left, 1 degrees each step
            camera_->frame()->action_turn(angle, camera_);
        } else if (e->key() == Qt::Key_Right && e->modifiers() == Qt::KeypadModifier) {
            auto angle = static_cast<float>(1 * M_PI / 180.0); // turn right, 1 degrees each step
            camera_->frame()->action_turn(-angle, camera_);
        } else if (e->key() == Qt::Key_Up && e->modifiers() == Qt::KeypadModifier) {    // move camera forward
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(0.0, 0.0, -step)));
        } else if (e->key() == Qt::Key_Down && e->modifiers() == Qt::KeypadModifier) {// move camera backward
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(0.0, 0.0, step)));
        } else if (e->key() == Qt::Key_Left &&
                   e->modifiers() == (Qt::KeypadModifier | Qt::ControlModifier)) {    // move camera left
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(-step, 0.0, 0.0)));
        } else if (e->key() == Qt::Key_Right &&
                   e->modifiers() == (Qt::KeypadModifier | Qt::ControlModifier)) {    // move camera right
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(step, 0.0, 0.0)));
        } else if (e->key() == Qt::Key_Up &&
                   e->modifiers() == (Qt::KeypadModifier | Qt::ControlModifier)) {    // move camera up
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(0.0, step, 0.0)));
        } else if (e->key() == Qt::Key_Down &&
                   e->modifiers() == (Qt::KeypadModifier | Qt::ControlModifier)) {    // move camera down
            float step = 0.05f * camera_->sceneRadius();
            camera_->frame()->translate(camera_->frame()->inverseTransformOf(vec3(0.0, -step, 0.0)));
        } else if (e->key() == Qt::Key_A && e->modifiers() == Qt::NoModifier) {
            if (drawable_axes_)
                drawable_axes_->set_visible(!drawable_axes_->is_visible());
        } else if (e->key() == Qt::Key_C && e->modifiers() == Qt::NoModifier) {
            if (currentModel())
                fitScreen(currentModel());
        } else if (e->key() == Qt::Key_F && e->modifiers() == Qt::NoModifier) {
            fitScreen();
        } else if (e->key() == Qt::Key_P && e->modifiers() == Qt::NoModifier) {
            if (camera_->type() == Camera::PERSPECTIVE)
                camera_->setType(Camera::ORTHOGRAPHIC);
            else
                camera_->setType(Camera::PERSPECTIVE);
        } else if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
            // Aligns camera
            Frame frame;
            frame.setTranslation(camera_->pivotPoint());
            camera_->frame()->alignWithFrame(&frame, true);

            // Aligns frame
            //if (manipulatedFrame())
            //	manipulatedFrame()->alignWithFrame(camera_->frame());
        } else if (e->key() == Qt::Key_Minus && e->modifiers() == Qt::ControlModifier)
            camera_->frame()->action_zoom(-1, camera_);
        else if (e->key() == Qt::Key_Equal && e->modifiers() == Qt::ControlModifier)
            camera_->frame()->action_zoom(1, camera_);

        else if (e->key() == Qt::Key_K && e->modifiers() == Qt::AltModifier) { // add key frame
            easy3d::Frame *frame = camera()->frame();
            camera()->keyframe_interpolator()->add_keyframe(*frame);
            // update scene bounding box to make sure the path is within the view frustum
            float old_radius = camera()->sceneRadius();
            float candidate_radius = distance(camera()->sceneCenter(), frame->position());
            camera()->setSceneRadius(std::max(old_radius, candidate_radius));
        } else if (e->key() == Qt::Key_D && e->modifiers() == Qt::ControlModifier) { // delete path
            camera()->keyframe_interpolator()->delete_path();

            // update scene bounding box
            Box3 box;
            for (auto m: models_)
                box.grow(m->bounding_box());
            camera_->setSceneBoundingBox(box.min_point(), box.max_point());
        } else if (e->key() == Qt::Key_K && e->modifiers() == Qt::ControlModifier) { // play the path
            if (camera()->keyframe_interpolator()->is_interpolation_started())
                camera()->keyframe_interpolator()->stop_interpolation();
            else
                camera()->keyframe_interpolator()->start_interpolation();
        } else if (e->key() == Qt::Key_BracketLeft && e->modifiers() == Qt::NoModifier) {
            for (auto m: models_) {
                for (auto d: m->renderer()->lines_drawables()) {
                    float size = d->line_width() - 1.0f;
                    if (size < 1)
                        size = 1;
                    d->set_line_width(size);
                }
            }
        } else if (e->key() == Qt::Key_BracketRight && e->modifiers() == Qt::NoModifier) {
            for (auto m: models_) {
                for (auto d: m->renderer()->lines_drawables()) {
                    float size = d->line_width() + 1.0f;
                    d->set_line_width(size);
                }
            }
        } else if (e->key() == Qt::Key_Minus && e->modifiers() == Qt::NoModifier) {
            for (auto m: models_) {
                for (auto d: m->renderer()->points_drawables()) {
                    float size = d->point_size() - 1.0f;
                    if (size < 1)
                        size = 1;
                    d->set_point_size(size);
                }
            }
        } else if (e->key() == Qt::Key_Equal && e->modifiers() == Qt::NoModifier) {
            for (auto m: models_) {
                for (auto d: m->renderer()->points_drawables()) {
                    float size = d->point_size() + 1.0f;
                    d->set_point_size(size);
                }
            }
        } else if (e->key() == Qt::Key_Comma && e->modifiers() == Qt::NoModifier) {
            int pre_idx = model_idx_;
            if (models_.empty())
                model_idx_ = -1;
            else
                model_idx_ = int((model_idx_ - 1 + models_.size()) % models_.size());
            if (model_idx_ != pre_idx) {
                if (model_idx_ >= 0)
                    LOG(INFO) << "current model: " << model_idx_ << ", " << models_[model_idx_]->name();
            }
        } else if (e->key() == Qt::Key_Period && e->modifiers() == Qt::NoModifier) {
            int pre_idx = model_idx_;
            if (models_.empty())
                model_idx_ = -1;
            else
                model_idx_ = int((model_idx_ + 1) % models_.size());
            if (model_idx_ != pre_idx) {
                if (model_idx_ >= 0)
                    LOG(INFO) << "current model: " << model_idx_ << ", " << models_[model_idx_]->name();
            }
        } else if (e->key() == Qt::Key_Delete && e->modifiers() == Qt::NoModifier) {
            if (currentModel())
                deleteModel(currentModel());
        } else if (e->key() == Qt::Key_E && e->modifiers() == Qt::NoModifier) {
            if (currentModel()) {
                auto *drawable = currentModel()->renderer()->get_lines_drawable("edges");
				if (drawable)
					drawable->set_visible(!drawable->is_visible());
            }
        } else if (e->key() == Qt::Key_V && e->modifiers() == Qt::NoModifier) {
            if (currentModel()) {
                auto drawable = currentModel()->renderer()->get_points_drawable("vertices");
				if (drawable)
					drawable->set_visible(!drawable->is_visible());
            }
        } else if (e->key() == Qt::Key_B && e->modifiers() == Qt::NoModifier) {
            auto mesh = dynamic_cast<SurfaceMesh *>(currentModel());
            if (mesh) {
                auto drawable = mesh->renderer()->get_lines_drawable("borders");
				if (drawable)
					drawable->set_visible(!drawable->is_visible());
            }
        } else if (e->key() == Qt::Key_L && e->modifiers() == Qt::NoModifier) { // locked vertices
            auto mesh = dynamic_cast<SurfaceMesh *>(currentModel());
            if (mesh) {
                auto drawable = mesh->renderer()->get_points_drawable("locks");
				if (drawable)
					drawable->set_visible(!drawable->is_visible());
            }
        } else if (e->key() == Qt::Key_M && e->modifiers() == Qt::NoModifier) {
            if (dynamic_cast<SurfaceMesh *>(currentModel())) {
                auto drawable = currentModel()->renderer()->get_triangles_drawable("faces");
                if (drawable) {
                    drawable->set_smooth_shading(!drawable->smooth_shading());
                }
            }
        } else if (e->key() == Qt::Key_D && e->modifiers() == Qt::NoModifier) {
            if (currentModel()) {
                std::ostream &output = std::cout;

                output << "----------- " << file_system::simple_name(currentModel()->name()) << " -----------\n";
                if (dynamic_cast<SurfaceMesh *>(currentModel())) {
                    auto model = dynamic_cast<SurfaceMesh *>(currentModel());
                    output << "model is a surface mesh. #face: " << std::to_string(model->n_faces())
                           << ", #vertex: " + std::to_string(model->n_vertices())
                           << ", #edge: " + std::to_string(model->n_edges()) << std::endl;
                } else if (dynamic_cast<PointCloud *>(currentModel())) {
                    auto model = dynamic_cast<PointCloud *>(currentModel());
                    output << "model is a point cloud. #vertex: " + std::to_string(model->n_vertices()) << std::endl;
                } else if (dynamic_cast<Graph *>(currentModel())) {
                    auto model = dynamic_cast<Graph *>(currentModel());
                    output << "model is a graph. #vertex: " + std::to_string(model->n_vertices())
                           << ", #edge: " + std::to_string(model->n_edges()) << std::endl;
                }

                if (!currentModel()->renderer()->points_drawables().empty()) {
                    output << "points drawables:\n";
                    for (auto d: currentModel()->renderer()->points_drawables())
                        d->buffer_stats(output);
                }
                if (!currentModel()->renderer()->lines_drawables().empty()) {
                    output << "lines drawables:\n";
                    for (auto d: currentModel()->renderer()->lines_drawables())
                        d->buffer_stats(output);
                }
                if (!currentModel()->renderer()->triangles_drawables().empty()) {
                    output << "triangles drawables:\n";
                    for (auto d: currentModel()->renderer()->triangles_drawables())
                        d->buffer_stats(output);
                }

                currentModel()->property_stats(output);
            }
        } else if (e->key() == Qt::Key_R && e->modifiers() == Qt::NoModifier) {
            // Reload the shader(s) - useful for writing/debugging shader code.
            ShaderManager::reload();
        }

        QOpenGLWidget::keyPressEvent(e);
        update();
    }


    void Viewer::keyReleaseEvent(QKeyEvent *e) {
        QOpenGLWidget::keyReleaseEvent(e);
        update();
    }


    void Viewer::timerEvent(QTimerEvent *e) {
        QOpenGLWidget::timerEvent(e);
        update();
    }


    void Viewer::closeEvent(QCloseEvent *e) {
        cleanup();
        QOpenGLWidget::closeEvent(e);
    }


    std::string Viewer::usage() const {
        return  " ------------------------------------------------------------------\n"
                " Easy3D viewer usage:                                              \n"
                " ------------------------------------------------------------------\n"
                "  F1:                  Help                                        \n"
                " ------------------------------------------------------------------\n"
                "  Ctrl + 'o':          Open file                                   \n"
                "  Ctrl + 's':          Save file                                   \n"
                "  Fn + Delete:         Delete current model                        \n"
                "  '<' or '>':          Switch between models                       \n"
                "  's':                 Snapshot                                    \n"
                " ------------------------------------------------------------------\n"
                "  'p':                 Toggle perspective/orthographic projection)	\n"
                "  Left:                Orbit-rotate the camera                     \n"
                "  Right:               Move up/down/left/right                     \n"
                "  Middle or Wheel:     Zoom in/out                                 \n"
                "  Ctrl + '+'/'-':      Zoom in/out                                 \n"
                "  Alt + Left:          Orbit-rotate the camera (screen based)      \n"
                "  Alt + Right:         Move up/down/left/right (screen based)      \n"
                "  Left/Right           Turn camera left/right                      \n"
                "  Ctrl + Left/Right:   Move camera left/right                      \n"
                "  Up/Down:             Move camera forward/backward                \n"
                "  Ctrl + Up/Down:      Move camera up/down                         \n"
                " ------------------------------------------------------------------\n"
                "  'f':                 Fit screen (all models)                     \n"
                "  'c':                 Fit screen (current model only)             \n"
                "  Shift + Left/Right:  Zoom to target/Zoom to fit screen           \n"
                " ------------------------------------------------------------------\n"
                "  '+'/'-':             Increase/Decrease point size (line width)   \n"
                "  'a':                 Toggle axes									\n"
                "  'b':                 Toggle borders								\n"
                "  'e':                 Toggle edges							    \n"
                "  'v':                 Toggle vertices                             \n"
                "  'm':                 Toggle smooth shading (for SurfaceMesh)     \n"
                "  'd':                 Print model info (drawables, properties)    \n"
                " ------------------------------------------------------------------\n";
    }


    void Viewer::addModel(Model *model) {
        if (!model) {
            LOG(WARNING) << "model is nullptr";
            return;
        }
        for (auto m: models_) {
            if (model == m) {
                LOG(WARNING) << "model has already been added to the viewer.";
                return;
            }
        }

        if (model->empty()) {
            LOG(WARNING) << "model does not have vertices. Only complete model can be added to the viewer.";
            return;
        }

        makeCurrent();
        model->set_renderer(std::make_shared<Renderer>(model));
        doneCurrent();

        int pre_idx = model_idx_;
        models_.push_back(model);
        model_idx_ = static_cast<int>(models_.size()) - 1; // make the last one current

        if (model_idx_ != pre_idx) {
            emit currentModelChanged();
            if (model_idx_ >= 0)
                LOG(INFO) << "current model: " << model_idx_ << ", " << models_[model_idx_]->name();
        }
    }


    void Viewer::deleteModel(Model *model) {
        if (!model) {
            LOG(WARNING) << "model is nullptr";
            return;
        }

        int pre_idx = model_idx_;
        auto pos = std::find(models_.begin(), models_.end(), model);
        if (pos != models_.end()) {
            const std::string name = model->name();
            models_.erase(pos);
            makeCurrent();  // make sure the context is current
            delete model;   // internally the renderer and manipulator will be deleted
            doneCurrent();
            model_idx_ = static_cast<int>(models_.size()) - 1; // make the last one current

            std::cout << "model deleted: " << name << std::endl;
        } else
            LOG(WARNING) << "no such model: " << model->name();

        if (model_idx_ != pre_idx) {
            emit currentModelChanged();
            if (model_idx_ >= 0)
                LOG(INFO) << "current model: " << model_idx_ << ", " << models_[model_idx_]->name();
        }
    }


	bool Viewer::addDrawable(Drawable *drawable) {
		if (!drawable) {
			LOG(WARNING) << "drawable is nullptr";
			return false;
		}
		for (auto d : drawables_) {
			if (drawable == d) {
				LOG(WARNING) << "drawable has already been added to the viewer.";
				return false;
			}
		}

		drawables_.push_back(drawable);
		return true;
	}


	bool Viewer::deleteDrawable(Drawable *drawable) {
        if (!drawable) {
            LOG(WARNING) << "drawable is nullptr";
            return false;
        }

        for (auto it = drawables_.begin(); it != drawables_.end(); ++it) {
            if (*it == drawable) {
                delete drawable;
                drawables_.erase(it);
                return true;
            }
        }

        // if the drawable was not found
        LOG(WARNING) << "no such drawable: " << drawable->name();
        return false;
	}


    void Viewer::fitScreen(const Model *model) {
        if (!model && models_.empty() && drawables_.empty()) 
            return;

        Box3 box;
        if (model)
            box = model->bounding_box();
        else {
            for (auto m: models_)
                box.grow(m->bounding_box());
			for (auto d : drawables_)
				box.grow(d->bounding_box());
        }
        camera_->setSceneBoundingBox(box.min_point(), box.max_point());
        camera_->showEntireScene();
        update();
    }


    vec3 Viewer::pointUnderPixel(const QPoint &p, bool &found) {
    // Liangliang: Qt6's QOpenGLWidget implementation is a black box. Using glReadPixels to read the depth buffer
    //      outside the paintGL() function has some undefined behaviors, even I made sure the context is made
    //      current and the defaultFramebufferObject() is bound. Below is what I observed:
    //        - it works well on macOS and Ubuntu, but not on Windows; it works on all three platforms if Qt5 is used;
    //        - on Windows, it works if the glReadPixels is called within the paintGL() function;
    //        - on Windows, when the function is called outside paintGL(), if saving the obtained depth buffer to an
    //          image, only some edge(s) of the image show correct depth information, and the majority part of the
    //          depth image shows black and very sparse white pixels.
    //      I guess the reason might be:
    //        - Depth buffer clearing: on some platforms (e.g., Windows), the depth buffer may be cleared or invalidated
    //          when Qt performs post rendering operations (e.g., compositing).
    //        - Driver specific implementation: Windows drivers may optimize FBO usage differently, especially if
    //          rendering results are only needed for display. This could affect the depth buffer availability outside
    //          paintGL().
    //      These platform-specific behaviors are hard to resolve without fully understanding the source code of the
    //      QOpenGLWidget class. It is bad the Qt keeps such implementation private, and it may change over the time.
    //
    //      Solution: rendering into my own framebuffer object with only a depth attachment and then reading the depth
    //      values. This ensures we have full control over the depth buffer and avoids relying on the QOpenGLWidget's
    //      internal framebuffer.

    makeCurrent();

    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int w = viewport[2];
    const int h = viewport[3];

    // Create an FBO with a depth attachment
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setSamples(0); // No multisampling
    QOpenGLFramebufferObject fbo(w, h, format);

    // Bind the framebuffer
    fbo.bind();
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer is not complete!" << std::endl;
        return vec3(0, 0, 0);
    }

    // Configure OpenGL state for rendering
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    if (!last_enable_depth_test)
        glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Render the scene
    draw();

    // Read the depth buffer
    // Qt (same as GLFW) uses upper corner for its origin while GL uses the lower corner.
    int glx = p.x();
    int gly = height() - 1 - p.y();
    // when dealing with OpenGL, all positions are relative to the viewer port. So we have to handle highdpi displays.
    glx = static_cast<int>(static_cast<float>(glx) * dpiScaling());
    gly = static_cast<int>(static_cast<float>(gly) * dpiScaling());
    float depth = std::numeric_limits<float>::max();
    glReadPixels(glx, gly, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

    // Unbind the framebuffer
    fbo.release();

    // Restore previous depth test states
    if (!last_enable_depth_test)
        glDisable(GL_DEPTH_TEST);

    doneCurrent();

    found = depth < 1.0f;
    if (found) {
        // The input to unprojectedCoordinatesOf() is defined in the screen coordinate system
        return camera_->unprojectedCoordinatesOf(vec3(static_cast<float>(p.x()), static_cast<float>(p.y()), depth));
    }

    return {};
}


    void Viewer::paintGL() {
        easy3d_debug_log_gl_error

#if 1
        // QOpenGLWidget renders everything into an FBO. Internally it changes
        // QSurfaceFormat to always have samples = 0 and the OpenGL context is
        // not a multisample context. So we have to query the render-buffer
        // to know if it is using multisampling. At initializeGL() we were not
        // able to query the actual samples because the internal FBO has not
        // been created yet, so we do it here.
        static bool queried = false;
        if (!queried) {
#if 1
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples_);
            easy3d_debug_log_frame_buffer_error
#else   // the samples can also be retrieved using glGetIntegerv()
            glGetIntegerv(GL_SAMPLES, &samples_); easy3d_debug_log_gl_error
#endif
            // warn the user if the expected request was not satisfied
            int samples = QSurfaceFormat::defaultFormat().samples();
            int max_num = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &max_num);
            if (samples > 0 && samples_ != samples) {
                if (samples_ == 0)
                    LOG(WARNING) << "MSAA is not available (" << samples << " samples requested)";
                else
                    LOG(WARNING) << "MSAA is available with " << samples_ << " samples (" << samples
                                 << " requested but max support is " << max_num << ")";
            } else VLOG(1) << "Samples received: " << samples_ << " (" << samples << " requested, max support is "
                           << max_num << ")";

            queried = true;
        }
#endif

        preDraw();

        draw();

        // Add visual hints: axis, camera, grid...
        postDraw();
    }


    void Viewer::drawCornerAxes() {
        ShaderProgram *program = ShaderManager::get_program("surface/surface");
        if (!program) {
            std::vector<ShaderProgram::Attribute> attributes;
            attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::POSITION, "vtx_position"));
            attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::TEXCOORD, "vtx_texcoord"));
            attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::COLOR, "vtx_color"));
            attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::NORMAL, "vtx_normal"));
            program = ShaderManager::create_program_from_files("surface/surface", attributes);
        }
        if (!program)
            return;

        if (!drawable_axes_) {
            const float base = 0.5f;   // the cylinder length, relative to the allowed region
            const float head = 0.2f;   // the cone length, relative to the allowed region
            std::vector<vec3> points, normals, colors;
            shape::create_cylinder(0.03, 10, vec3(0, 0, 0), vec3(base, 0, 0), vec3(1, 0, 0), points, normals, colors);
            shape::create_cylinder(0.03, 10, vec3(0, 0, 0), vec3(0, base, 0), vec3(0, 1, 0), points, normals, colors);
            shape::create_cylinder(0.03, 10, vec3(0, 0, 0), vec3(0, 0, base), vec3(0, 0, 1), points, normals, colors);
            shape::create_cone(0.06, 20, vec3(base, 0, 0), vec3(base + head, 0, 0), vec3(1, 0, 0), points, normals,
                                colors);
            shape::create_cone(0.06, 20, vec3(0, base, 0), vec3(0, base + head, 0), vec3(0, 1, 0), points, normals,
                                colors);
            shape::create_cone(0.06, 20, vec3(0, 0, base), vec3(0, 0, base + head), vec3(0, 0, 1), points, normals,
                                colors);
            shape::create_sphere(vec3(0, 0, 0), 0.06, 20, 20, vec3(0, 1, 1), points, normals, colors);
            drawable_axes_ = new TrianglesDrawable("corner_axes");
            drawable_axes_->update_vertex_buffer(points);
            drawable_axes_->update_normal_buffer(normals);
            drawable_axes_->update_color_buffer(colors);
            drawable_axes_->set_property_coloring(State::VERTEX);
        }
        if (!drawable_axes_->is_visible())
            return;

        // The viewport is changed to fit the lower left corner.
        int viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        static const int corner_frame_size = static_cast<int>(100 * dpiScaling());
        glViewport(0, 0, corner_frame_size, corner_frame_size);

        // To make the axis appear over other objects: reserve a tiny bit of the
        // front depth range. NOTE: do remember to restore it later.
        glDepthRangef(0, 0.01f);

        const mat4 &proj = transform::ortho(-1, 1, -1, 1, -1, 1);
        const mat4 &view = camera_->orientation().inverse().matrix();
        const mat4 &MVP = proj * view;

        // camera position is defined in world coordinate system.
        const vec3 &wCamPos = camera_->position();
        // it can also be computed as follows:
        //const vec3& wCamPos = invMV * vec4(0, 0, 0, 1);
        const mat4 &MV = camera_->modelViewMatrix();
        const vec4 &wLightPos = inverse(MV) * setting::light_position;

        program->bind();
        program->set_uniform("MVP", MVP)
                ->set_uniform("MANIP", mat4::identity())
                ->set_uniform("NORMAL", mat3::identity())   // needs be padded when using uniform blocks
                ->set_uniform("lighting", true)
                ->set_uniform("two_sides_lighting", false)
                ->set_uniform("smooth_shading", true)
                ->set_uniform("wLightPos", wLightPos)
                ->set_uniform("wCamPos", wCamPos)
                ->set_uniform("ssaoEnabled", false)
                ->set_uniform("per_vertex_color", true)
                ->set_uniform("distinct_back_color", false)
                ->set_block_uniform("Material", "ambient", setting::material_ambient)
                ->set_block_uniform("Material", "specular", setting::material_specular)
                ->set_block_uniform("Material", "shininess", &setting::material_shininess)
                ->set_uniform("highlight", false)
                ->set_uniform("clippingPlaneEnabled", false)
                ->set_uniform("selected", false)
                ->set_uniform("highlight_color", setting::highlight_color)
                ->set_uniform("use_texture", false);
        drawable_axes_->gl_draw();
        program->release();

        // restore the viewport
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glDepthRangef(0.0f, 1.0f);
    }


    void Viewer::preDraw() {
        // The Qt6 documentation says (https://doc.qt.io/qt-6/qopenglwidget.html#paintGL):
        //      Default implementation performs a glClear(). Subclasses are not expected to invoke
        //      the base class implementation and should perform clearing on their own.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }


    void Viewer::postDraw() {
        // draw the Easy3D logo and GPU time
        if (texter_ && texter_->num_fonts() >= 2) {
            const float font_size = 15.0f;
            const float offset = 20.0f * dpiScaling();
            texter_->draw("Easy3D", offset, offset, font_size, 0);

            // FPS computation
            static unsigned int fps_count = 0;
            static double fps = 0.0;
            static const unsigned int max_count = 40;
            static QString fps_string("fps: ??");
            if (++fps_count == max_count) {
                fps = 1000.0 * max_count / static_cast<double>(timer_.restart());
                fps_string = tr("fps: %1").arg(fps, 0, 'f', ((fps < 10.0) ? 1 : 0));
                fps_count = 0;
            }
            texter_->draw(fps_string.toStdString(), offset, 50.0f * dpiScaling(), 16, 1);
        }

        if (show_pivot_point_) {
            ShaderProgram *program = ShaderManager::get_program("lines/lines_plain_color");
            if (!program) {
                std::vector<ShaderProgram::Attribute> attributes;
                attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::POSITION, "vtx_position"));
                attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::COLOR, "vtx_color"));
                program = ShaderManager::create_program_from_files("lines/lines_plain_color", attributes);
            }
            if (!program)
                return;

            const float size = 10;
            LinesDrawable drawable("pivot_point");
            const vec3 &pivot = camera()->projectedCoordinatesOf(camera()->pivotPoint());
            std::vector<vec3> points = {
                    vec3(pivot.x - size, pivot.y, 0.5f), vec3(pivot.x + size, pivot.y, 0.5f),
                    vec3(pivot.x, pivot.y - size, 0.5f), vec3(pivot.x, pivot.y + size, 0.5f)
            };
            drawable.update_vertex_buffer(points);

            const mat4 &proj = transform::ortho(0.0f, static_cast<float>(width()), static_cast<float>(height()), 0.0f,
                                                0.0f,
                                                -1.0f);
            glDisable(GL_DEPTH_TEST);   // always on top
            program->bind();
            program->set_uniform("MVP", proj);
            program->set_uniform("per_vertex_color", false);
            program->set_uniform("default_color", vec4(0.0f, 0.0f, 1.0f, 1.0f));
            drawable.gl_draw();
            program->release();
            glEnable(GL_DEPTH_TEST);   // restore
        }

        drawCornerAxes();
    }


    void Viewer::draw() {
#if 0
        // Example code: split the view to have different rendering styles for the same object/scene.
        glEnable(GL_SCISSOR_TEST);
        int viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        glScissor(viewport[0], viewport[1], viewport[2] * 0.5f, viewport[3]);
        for (const auto m : models_) {
            for (auto d : m->renderer()->triangles_drawables())
                d->draw(camera());
        }
        glScissor(viewport[2] * 0.5f, viewport[1], viewport[2] * 0.5f, viewport[3]);
        for (const auto m : models_) {
            for (auto d : m->renderer()->lines_drawables())
                d->draw(camera());
        }
        glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);

#else 

        easy3d_debug_log_gl_error

        for (const auto m: models_) {
            if (!m->renderer()->is_visible())
                continue;

            // temporarily change the depth range and depth comparison method to properly render edges.
            glDepthRange(0.001, 1.0);
            for (auto d: m->renderer()->triangles_drawables()) {
                if (d->is_visible())
                    d->draw(camera());
                easy3d_debug_log_gl_error
            }

            glDepthRange(0.0, 1.0);
            glDepthFunc(GL_LEQUAL);
            for (auto d: m->renderer()->lines_drawables()) {
                if (d->is_visible())
                    d->draw(camera());
                easy3d_debug_log_gl_error
            }
            glDepthFunc(GL_LESS);

            for (auto d: m->renderer()->points_drawables()) {
                if (d->is_visible())
                    d->draw(camera());
                easy3d_debug_log_gl_error
            }
        }

		for (auto d : drawables_) {
			if (d->is_visible())
				d->draw(camera());
		}
#endif
    }

    // \endcond

}