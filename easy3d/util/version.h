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

#ifndef EASY3D_UTIL_VERSION_H
#define EASY3D_UTIL_VERSION_H


#include <string>


namespace easy3d {

    /// returns the major version number of Easy3D.
    int version_major();

    /// returns the major version number of Easy3D.
    int version_minor();

    /// returns the minor version number of Easy3D.
    int version_patch();

    /// returns the version string of Easy3D, in the format [MAJOR].[MINOR].[PATCH], e.g., “2.5.3”.
    std::string version_string();

    /// returns the version number of Easy3D, in the format 10[MAJOR]0[MINOR]0[PATCH], e.g., 1020503.
    std::size_t version_number();

    /// returns the release date of Easy3D, in the format YYYYMMDD, e.g., 20240305.
    std::size_t release_date();
}

/// Easy3D version string, in the format [MAJOR].[MINOR].[PATCH], e.g., “2.5.3”.
#define EASY3D_VERSION_STR Easy3D_VERSION_STRING

/// Easy3D version number, in the format 10[MAJOR]0[MINOR]0[PATCH].
#define EASY3D_VERSION_NR 1020601

/// Easy3D release date, in the format YYYYMMDD.
#define EASY3D_RELEASE_DATE 20250611


#endif  // EASY3D_UTIL_VERSION_H
