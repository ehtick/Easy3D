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

#include <easy3d/viewer/viewer.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/texture_manager.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/resource.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/timer.h>

using namespace easy3d;


int test_texture(int duration) {
    Viewer viewer("Texture");
    viewer.camera()->setUpVector(vec3(0, 1, 0));
    viewer.camera()->setViewDirection(vec3(0, 0, -1));

    //----------------------- Load texture from an image file ------------------------

    const std::string texture_file = resource::directory() + "/images/logo.jpg";
    Texture *tex = TextureManager::request(texture_file);
    if (!tex) {
        LOG(ERROR) << "failed to create texture. Please make sure the file exists and format is correct.";
        return EXIT_FAILURE;
    }

    //--------------- create a mesh (which contains a single quad) -------------------

    auto mesh = new SurfaceMesh;
    auto texcoord = mesh->add_vertex_property<vec2>("v:texcoord");

    const auto w = static_cast<float>(tex->width());
    const auto h = static_cast<float>(tex->height());
    // create a quad face having an aspect ratio the same as the texture image
    auto v0 = mesh->add_vertex(vec3(0, 0, 0));
    texcoord[v0] = vec2(0, 0);
    auto v1 = mesh->add_vertex(vec3(w, 0, 0));
    texcoord[v1] = vec2(1, 0);
    auto v2 = mesh->add_vertex(vec3(w, h, 0));
    texcoord[v2] = vec2(1, 1);
    auto v3 = mesh->add_vertex(vec3(0, h, 0));
    texcoord[v3] = vec2(0, 1);
    mesh->add_quad(v0, v1, v2, v3);

    // add the model to the viewer and create the default drawable "faces"
    viewer.add_model(std::shared_ptr<SurfaceMesh>(mesh), true);

    // set the texture of the default drawable "faces"
    auto drawable = mesh->renderer()->get_triangles_drawable("faces");
    drawable->set_texture(tex);
    drawable->set_texture_coloring(easy3d::State::VERTEX, "v:texcoord", tex);

    viewer.set_usage("testing texture...");

    Timer<>::single_shot(duration, (Viewer*)&viewer, &Viewer::exit);
    return viewer.run();
}

