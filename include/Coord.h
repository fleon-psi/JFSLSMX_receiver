/*
 * Copyright 2020 Paul Scherrer Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INDEX_COORD_H
#define INDEX_COORD_H

#include <ostream>

class Coord {
public:
    double x,y,z;
    Coord();
    Coord(const double in[3]);
    Coord(double x, double y, double z);
    Coord operator+(const Coord &in) const;
    Coord operator-(const Coord &in) const;
    Coord operator*(double in) const;
    Coord operator/(double in) const;

    Coord& operator+=(const Coord &in);
    Coord& operator-=(const Coord &in);
    Coord& operator*=(double in);
    Coord& operator/=(double in);

    Coord operator%(const Coord &in) const; // Cross product
    double operator*(const Coord &in) const; // Dot product
    double Length() const;
    Coord Normalize() const;

    friend std::ostream &operator<<( std::ostream &output, const Coord &in );
};

Coord operator*(double in1, const Coord& in2);
Coord operator*(const double M[3][3], const Coord& in);

Coord convert_spherical(double r, double theta, double phi);

double determinant(const Coord &in1, const Coord &in2, const Coord &in3);

// |b2| <= |b1|
void GaussianReduction(const Coord b1, const Coord b2, Coord &a, Coord &b, double min_len);
void SemaevReduction(Coord &a, Coord &b, Coord &c);

#endif //INDEX_COORD_H
