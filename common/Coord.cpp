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

#include <cmath>
#include "../include/Coord.h"

Coord::Coord() {
    x = 0.0; y = 0.0; z = 0.0;
}

Coord::Coord(const double in[3]) {
    x = in[0];
    y = in[1];
    z = in[2];
}

Coord::Coord(double in_x, double in_y, double in_z) {
    x = in_x;
    y = in_y;
    z = in_z;
}

Coord Coord::operator+(const Coord &in) const {
    return Coord(this->x+in.x, this->y+in.y, this->z+in.z);
}

Coord Coord::operator-(const Coord &in) const {
    return Coord(this->x-in.x, this->y-in.y, this->z-in.z);
}

Coord Coord::operator*(double in) const {
    return Coord(this->x*in, this->y*in, this->z*in);
}

Coord Coord::operator/(double in) const {
    return Coord(this->x/in, this->y/in, this->z/in);
};

Coord& Coord::operator+=(const Coord &in) {
    this->x += in.x;
    this->y += in.y;
    this->z += in.z;
    return *this;
}

Coord& Coord::operator-=(const Coord &in) {
    this->x -= in.x;
    this->y -= in.y;
    this->z -= in.z;
    return *this;
}

Coord& Coord::operator*=(double in) {
    this->x *= in;
    this->y *= in;
    this->z *= in;
    return *this;
}

Coord& Coord::operator/=(double in) {
    this->x /= in;
    this->y /= in;
    this->z /= in;
    return *this;
}

Coord Coord::operator%(const Coord &in) const {
    return Coord(this->y * in.z - this->z * in.y,
            this->x * in.z - this->z * in.x,
            this->x * in.y - this->y * in.x);
}; // Cross product

double Coord::operator*(const Coord &in) const {
    return this->x * in.x + this->y * in.y + this->z * in.z;
};


double Coord::Length() const {
    return sqrt(this->x*this->x + this->y*this->y + this->z*this->z);
}

Coord Coord::Normalize() const {
    double len = Length();
    return Coord(this->x/len, this->y/len, this->z/len);
}

Coord operator*(const double M[3][3], const Coord& in) {
    return Coord(M[0][0] * in.x + M[0][1] * in.y + M[0][2] * in.z,
                 M[1][0] * in.x + M[1][1] * in.y + M[1][2] * in.z,
                 M[2][0] * in.x + M[2][1] * in.y + M[2][2] * in.z);
}

Coord convert_spherical(double r, double theta, double phi) {
    return Coord(r * sin(theta) * cos(phi),
            r * sin(theta) * sin(phi),
            r * cos(theta));
}

Coord operator*(double in1, const Coord& in2) {
    return in2 * in1;
}

double determinant(const Coord &in1, const Coord &in2, const Coord &in3) {
    return (in1 % in2) * in3;
}

std::ostream &operator<<( std::ostream &output, const Coord &in ) {
    output << in.x << " " << in.y << " " << in.z;
    return output;
}


void GaussianReduction(const Coord b1, const Coord b2, Coord &a, Coord &b, double min_len) {
    // |b2| <= |b1|
    if (b2*b2 > b1*b1) GaussianReduction(b2, b1, a, b, min_len);
    else {
        double r = std::round((b1 * b2) / (b2 * b2));
        Coord b3 = b1 - r * b2;
        if (b3.Length() < min_len) b = b2, a = Coord(0,0,0);
        else if ((b3 * b3) < (b2 * b2)) GaussianReduction(b2, b3, a, b, min_len);
        else a = b3, b = b2;
    }
}

void SemaevReduction(Coord &a, Coord &b, Coord &c) {

}
