/**
 * Copyright (C) 2015 by Liangliang Nan (liangliang.nan@gmail.com)
 * https://3d.bk.tudelft.nl/liangliang/
 *
 * This file is part of Easy3D. If it is useful in your research/work,
 * I would be grateful if you show your appreciation by citing it:
 * ------------------------------------------------------------------
 *      Liangliang Nan.
 *      Easy3D: a lightweight, easy-to-use, and efficient C++
 *      library for processing and rendering 3D data. 2018.
 * ------------------------------------------------------------------
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
 */

#include <easy3d/viewer/state.h>
#include <easy3d/viewer/setting.h>
#include <easy3d/util/logging.h>


namespace easy3d {


    State::Material::Material()
            : ambient(setting::material_ambient), specular(setting::material_specular),
              shininess(setting::material_shininess) {

    }

    State::Material::Material(const vec3 &ambi, const vec3 &spec, float shin)
            : ambient(ambi), specular(spec), shininess(shin) {

    }

    State::State()
            : visible_(true), coloring_method_(UNIFORM_COLOR), color_(0.8f, 0.8f, 0.8f, 1.0f),
              property_location_(VERTEX), property_name_("uniform color"), lighting_(true),
              lighting_two_sides_(setting::light_two_sides), distinct_back_color_(setting::light_distinct_back_color),
              back_color_(setting::light_back_color), highlight_(false), highlight_range_(-1, -1), texture_(nullptr),
              texture_repeat_(1.0f), texture_fractional_repeat_(0.0f), clamp_range_(true), clamp_lower_(0.05f),
              clamp_upper_(0.05f) {
    }


    void State::set_uniform_coloring(const vec4 &color) {
        coloring_method_ = UNIFORM_COLOR;
        color_ = color;
        property_location_ = VERTEX;
        property_name_ = "uniform color";
    }


    void State::set_property_coloring(PropertyLocation color_location, const std::string &color_name) {
        coloring_method_ = COLOR_PROPERTY;
        property_location_ = color_location;
        property_name_ = color_name;
    }


    void State::set_texture_coloring(PropertyLocation texcoord_location, const std::string &texcoord_name,
                                     const Texture *texture, float repeat, float repeat_fraction) {
        coloring_method_ = TEXTURED;
        property_location_ = texcoord_location;
        property_name_ = texcoord_name;
        texture_ = texture;
        texture_repeat_ = repeat;
        texture_fractional_repeat_ = repeat_fraction;
    }


    void State::set_scalar_coloring(PropertyLocation scalar_location, const std::string &scalar_name,
                                    const Texture *texture, float clamp_lower, float clamp_upper) {
            coloring_method_ = SCALAR_FIELD;
            property_location_ = scalar_location;
            property_name_ = scalar_name;
            texture_ = texture;
            texture_repeat_ = 1.0f;
            texture_fractional_repeat_ = 0.0f;
            clamp_lower_ = clamp_lower;
            clamp_upper_ = clamp_upper;
        }


        void State::set_coloring(Method method, PropertyLocation location, const std::string &name) {
            coloring_method_ = method;
            property_location_ = location;
            property_name_ = name;
        }

    }