#define _CRT_SECURE_NO_WARNINGS 1
#include <vector>
 
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
 
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <thread>
#include <random>
#include <algorithm>
#include <string>
#include <stdio.h>
#include <stdexcept>

#define PI 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821

#if defined (_MSC_VER)  // Visual studio
    #define thread_local __declspec( thread )
#elif defined (__GCC__) // GCC
    #define thread_local __thread
#endif

class Vector {
public:
    explicit Vector(double x = 0, double y = 0, double z = 0) {
        data[0] = x;
        data[1] = y;
        data[2] = z;
    }
    double norm2() const {
        return data[0] * data[0] + data[1] * data[1] + data[2] * data[2];
    }
    double norm() const {
        return sqrt(norm2());
    }
    void normalize() {
        double n = norm();
        data[0] /= n;
        data[1] /= n;
        data[2] /= n;
    }
    double operator[](int i) const { return data[i]; };
    double& operator[](int i) { return data[i]; };
    double data[3];
};
 
Vector operator+(const Vector& a, const Vector& b) {
    return Vector(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}
Vector operator-(const Vector& a, const Vector& b) {
    return Vector(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}
Vector operator-(const Vector& a){
    return Vector(-a[0], -a[1], -a[2]);
}
Vector operator*(const double a, const Vector& b) {
    return Vector(a*b[0], a*b[1], a*b[2]);
}
Vector operator*(const Vector& a, const double b) {
    return Vector(a[0]*b, a[1]*b, a[2]*b);
}
Vector operator/(const Vector& a, const double b) {
    return Vector(a[0] / b, a[1] / b, a[2] / b);
}
double dot(const Vector& a, const Vector& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector cross(const Vector& a, const Vector& b) {
    return Vector(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
}

class Ray {
public:
    Vector origin;
    Vector unit;
    explicit Ray(Vector o, Vector u){
        origin = o;
        unit = u;
        unit.normalize();
    }
};

class TriangleIndices {
public:
	TriangleIndices(int vtxi = -1, int vtxj = -1, int vtxk = -1, int ni = -1, int nj = -1, int nk = -1, int uvi = -1, int uvj = -1, int uvk = -1, int group = -1, bool added = false) : vtxi(vtxi), vtxj(vtxj), vtxk(vtxk), uvi(uvi), uvj(uvj), uvk(uvk), ni(ni), nj(nj), nk(nk), group(group) {
	};
	int vtxi, vtxj, vtxk; // indices within the vertex coordinates array
	int uvi, uvj, uvk;  // indices within the uv coordinates array
	int ni, nj, nk;  // indices within the normals array
	int group;       // face group
};

struct Intersection {
    bool flag;
    Vector position;
    double t;
    bool inside;
    Vector normal;
    Intersection(bool fla, Vector pos, double ti, bool insid, Vector norm) : flag(fla), position(pos), t(ti), inside(insid), normal(norm) {}
};

struct Cast{
    Intersection intersect;
    Vector albedo;
    bool mirror;
    bool transp;
    double refraction;
    // If necessary, add pointer to geometry
    Cast(Intersection inter, Vector alb, double refr) : intersect(inter), albedo(alb), refraction(refr) {
        if (refraction == -1){
            mirror = false;
            transp = false;
        }
        else if (refraction == 0){
            transp = false;
            mirror = true;
        }
        else{
            transp = true;
            mirror = false;
        }
    }
};

class Geometry{
    public:
        virtual ~Geometry() {}
        Vector origin;
        Vector (*movement)(double);
        double refraction;
        virtual Cast intersect(Ray &r, double time) = 0;
};

Vector constant_position(double t){return Vector(0,0,0);}

Vector ninja_movement_yellow(double t){
    // Diameter is 6 so movement ~5
    if (t<0.075){return Vector(-5, 0, 0);}
    else if (t<0.425){return Vector(0,0,0);}
    else if (t<0.5){return Vector(5,0,0);}
    else {return Vector(24*(t-0.75),0,0);}
}

Vector throw_movement(double t){
    // Make more samples near the end of the trajectory 
    double tp = (sqrt(t)+t)/2;
    return Vector(8*tp,25*tp - 20*pow(tp, 2),0);
}

Intersection triangle_intersect(Vector A, Vector B, Vector C, Ray &r, Vector normalA, Vector normalB, Vector normalC){
    Vector e1 = B-A;
    Vector e2 = C-A;
    Vector N = cross(e1, e2);
    double dotUN = dot(r.unit, N);
    if (dotUN == 0){
        return Intersection(false, Vector(0,0,0), 0, false, N);
    }
    double beta = dot(e2, cross(A - r.origin, r.unit))/dotUN;
    double gamma = -dot(e1, cross(A - r.origin, r.unit))/dotUN;
    double alpha = 1 - beta - gamma;
    double t = dot(A - r.origin, N)/dotUN;
    if (0<=alpha && alpha<=1 && 0<=beta && beta<=1 && 0<=gamma && gamma<=1 && t>0){
        Vector shading_normal = alpha * normalA + beta * normalB + gamma * normalC;
        shading_normal.normalize();
        return Intersection(true, A + beta*e1 + gamma*e2, t, false, shading_normal);
    }
    return Intersection(false, Vector(0,0,0), 0, false, Vector(0,0,1));
}

class TriangleMesh : public Geometry {
public:
    ~TriangleMesh() {}

    explicit TriangleMesh(const char* obj, Vector ori, double rescale = 1, double refr = -1, Vector (*m)(double) = &constant_position){
        readOBJ(obj);
        origin = ori;
        refraction = -1;
        movement = m;
        if (rescale != 1){
            for (size_t i = 0; i<vertices.size(); i++){
                vertices[i] = vertices[i]*rescale;
            }
        }
    }
	
    Cast intersect(Ray &r, double time) override {
        if (indices.size() == 0){
            return Cast(Intersection(false, r.origin, 0, false, Vector(0,0,1)), Vector(0,0,0), -1);
        }
        TriangleIndices index = indices[0];
        Vector origint = origin + movement(time);
        Intersection best_inter = triangle_intersect(origint+vertices[index.vtxi], origint+vertices[index.vtxj], origint+vertices[index.vtxk], r, normals[index.ni], normals[index.nj], normals[index.nk]);
        size_t best_index = 0;
        for (size_t i=1; i<indices.size(); i++){
            index = indices[i];
            Intersection current_intersect = triangle_intersect(origint+vertices[index.vtxi], origint+vertices[index.vtxj], origint+vertices[index.vtxk], r, normals[index.ni], normals[index.nj], normals[index.nk]);
            if (best_inter.flag == false || (current_intersect.flag == true && current_intersect.t < best_inter.t)){
                best_inter = current_intersect;
                best_index = i;
            }
        }
        return Cast(best_inter, Vector(255, 255, 242), refraction); // TODO update uvs
    }
    
	void readOBJ(const char* obj) {

		char grp[255];

		FILE* f;
		f = fopen(obj, "r");
		int curGroup = -1;
		while (!feof(f)) {
			char line[255];
			if (!fgets(line, 255, f)) break;

			std::string linetrim(line);
			linetrim.erase(linetrim.find_last_not_of(" \r\t") + 1);
			strcpy(line, linetrim.c_str());

			if (line[0] == 'u' && line[1] == 's') {
				sscanf(line, "usemtl %[^\n]\n", grp);
				curGroup++;
			}

			if (line[0] == 'v' && line[1] == ' ') {
				Vector vec;

				Vector col;
				if (sscanf(line, "v %lf %lf %lf %lf %lf %lf\n", &vec[0], &vec[1], &vec[2], &col[0], &col[1], &col[2]) == 6) {
					col[0] = std::min(1., std::max(0., col[0]));
					col[1] = std::min(1., std::max(0., col[1]));
					col[2] = std::min(1., std::max(0., col[2]));

					vertices.push_back(vec);
					vertexcolors.push_back(col);

				} else {
					sscanf(line, "v %lf %lf %lf\n", &vec[0], &vec[1], &vec[2]);
					vertices.push_back(vec);
				}
			}
			if (line[0] == 'v' && line[1] == 'n') {
				Vector vec;
				sscanf(line, "vn %lf %lf %lf\n", &vec[0], &vec[1], &vec[2]);
				normals.push_back(vec);
			}
			if (line[0] == 'v' && line[1] == 't') {
				Vector vec;
				sscanf(line, "vt %lf %lf\n", &vec[0], &vec[1]);
				uvs.push_back(vec);
			}
			if (line[0] == 'f') {
				TriangleIndices t;
				int i0, i1, i2, i3;
				int j0, j1, j2, j3;
				int k0, k1, k2, k3;
				int nn;
				t.group = curGroup;

				char* consumedline = line + 1;
				int offset;

				nn = sscanf(consumedline, "%u/%u/%u %u/%u/%u %u/%u/%u%n", &i0, &j0, &k0, &i1, &j1, &k1, &i2, &j2, &k2, &offset);
				if (nn == 9) {
					if (i0 < 0) t.vtxi = vertices.size() + i0; else	t.vtxi = i0 - 1;
					if (i1 < 0) t.vtxj = vertices.size() + i1; else	t.vtxj = i1 - 1;
					if (i2 < 0) t.vtxk = vertices.size() + i2; else	t.vtxk = i2 - 1;
					if (j0 < 0) t.uvi = uvs.size() + j0; else	t.uvi = j0 - 1;
					if (j1 < 0) t.uvj = uvs.size() + j1; else	t.uvj = j1 - 1;
					if (j2 < 0) t.uvk = uvs.size() + j2; else	t.uvk = j2 - 1;
					if (k0 < 0) t.ni = normals.size() + k0; else	t.ni = k0 - 1;
					if (k1 < 0) t.nj = normals.size() + k1; else	t.nj = k1 - 1;
					if (k2 < 0) t.nk = normals.size() + k2; else	t.nk = k2 - 1;
					indices.push_back(t);
				} else {
					nn = sscanf(consumedline, "%u/%u %u/%u %u/%u%n", &i0, &j0, &i1, &j1, &i2, &j2, &offset);
					if (nn == 6) {
						if (i0 < 0) t.vtxi = vertices.size() + i0; else	t.vtxi = i0 - 1;
						if (i1 < 0) t.vtxj = vertices.size() + i1; else	t.vtxj = i1 - 1;
						if (i2 < 0) t.vtxk = vertices.size() + i2; else	t.vtxk = i2 - 1;
						if (j0 < 0) t.uvi = uvs.size() + j0; else	t.uvi = j0 - 1;
						if (j1 < 0) t.uvj = uvs.size() + j1; else	t.uvj = j1 - 1;
						if (j2 < 0) t.uvk = uvs.size() + j2; else	t.uvk = j2 - 1;
						indices.push_back(t);
					} else {
						nn = sscanf(consumedline, "%u %u %u%n", &i0, &i1, &i2, &offset);
						if (nn == 3) {
							if (i0 < 0) t.vtxi = vertices.size() + i0; else	t.vtxi = i0 - 1;
							if (i1 < 0) t.vtxj = vertices.size() + i1; else	t.vtxj = i1 - 1;
							if (i2 < 0) t.vtxk = vertices.size() + i2; else	t.vtxk = i2 - 1;
							indices.push_back(t);
						} else {
							nn = sscanf(consumedline, "%u//%u %u//%u %u//%u%n", &i0, &k0, &i1, &k1, &i2, &k2, &offset);
							if (i0 < 0) t.vtxi = vertices.size() + i0; else	t.vtxi = i0 - 1;
							if (i1 < 0) t.vtxj = vertices.size() + i1; else	t.vtxj = i1 - 1;
							if (i2 < 0) t.vtxk = vertices.size() + i2; else	t.vtxk = i2 - 1;
							if (k0 < 0) t.ni = normals.size() + k0; else	t.ni = k0 - 1;
							if (k1 < 0) t.nj = normals.size() + k1; else	t.nj = k1 - 1;
							if (k2 < 0) t.nk = normals.size() + k2; else	t.nk = k2 - 1;
							indices.push_back(t);
						}
					}
				}

				consumedline = consumedline + offset;

				while (true) {
					if (consumedline[0] == '\n') break;
					if (consumedline[0] == '\0') break;
					nn = sscanf(consumedline, "%u/%u/%u%n", &i3, &j3, &k3, &offset);
					TriangleIndices t2;
					t2.group = curGroup;
					if (nn == 3) {
						if (i0 < 0) t2.vtxi = vertices.size() + i0; else	t2.vtxi = i0 - 1;
						if (i2 < 0) t2.vtxj = vertices.size() + i2; else	t2.vtxj = i2 - 1;
						if (i3 < 0) t2.vtxk = vertices.size() + i3; else	t2.vtxk = i3 - 1;
						if (j0 < 0) t2.uvi = uvs.size() + j0; else	t2.uvi = j0 - 1;
						if (j2 < 0) t2.uvj = uvs.size() + j2; else	t2.uvj = j2 - 1;
						if (j3 < 0) t2.uvk = uvs.size() + j3; else	t2.uvk = j3 - 1;
						if (k0 < 0) t2.ni = normals.size() + k0; else	t2.ni = k0 - 1;
						if (k2 < 0) t2.nj = normals.size() + k2; else	t2.nj = k2 - 1;
						if (k3 < 0) t2.nk = normals.size() + k3; else	t2.nk = k3 - 1;
						indices.push_back(t2);
						consumedline = consumedline + offset;
						i2 = i3;
						j2 = j3;
						k2 = k3;
					} else {
						nn = sscanf(consumedline, "%u/%u%n", &i3, &j3, &offset);
						if (nn == 2) {
							if (i0 < 0) t2.vtxi = vertices.size() + i0; else	t2.vtxi = i0 - 1;
							if (i2 < 0) t2.vtxj = vertices.size() + i2; else	t2.vtxj = i2 - 1;
							if (i3 < 0) t2.vtxk = vertices.size() + i3; else	t2.vtxk = i3 - 1;
							if (j0 < 0) t2.uvi = uvs.size() + j0; else	t2.uvi = j0 - 1;
							if (j2 < 0) t2.uvj = uvs.size() + j2; else	t2.uvj = j2 - 1;
							if (j3 < 0) t2.uvk = uvs.size() + j3; else	t2.uvk = j3 - 1;
							consumedline = consumedline + offset;
							i2 = i3;
							j2 = j3;
							indices.push_back(t2);
						} else {
							nn = sscanf(consumedline, "%u//%u%n", &i3, &k3, &offset);
							if (nn == 2) {
								if (i0 < 0) t2.vtxi = vertices.size() + i0; else	t2.vtxi = i0 - 1;
								if (i2 < 0) t2.vtxj = vertices.size() + i2; else	t2.vtxj = i2 - 1;
								if (i3 < 0) t2.vtxk = vertices.size() + i3; else	t2.vtxk = i3 - 1;
								if (k0 < 0) t2.ni = normals.size() + k0; else	t2.ni = k0 - 1;
								if (k2 < 0) t2.nj = normals.size() + k2; else	t2.nj = k2 - 1;
								if (k3 < 0) t2.nk = normals.size() + k3; else	t2.nk = k3 - 1;								
								consumedline = consumedline + offset;
								i2 = i3;
								k2 = k3;
								indices.push_back(t2);
							} else {
								nn = sscanf(consumedline, "%u%n", &i3, &offset);
								if (nn == 1) {
									if (i0 < 0) t2.vtxi = vertices.size() + i0; else	t2.vtxi = i0 - 1;
									if (i2 < 0) t2.vtxj = vertices.size() + i2; else	t2.vtxj = i2 - 1;
									if (i3 < 0) t2.vtxk = vertices.size() + i3; else	t2.vtxk = i3 - 1;
									consumedline = consumedline + offset;
									i2 = i3;
									indices.push_back(t2);
								} else {
									consumedline = consumedline + 1;
								}
							}
						}
					}
				}

			}

		}
		fclose(f);

	}

	std::vector<TriangleIndices> indices;
	std::vector<Vector> vertices;
	std::vector<Vector> normals;
	std::vector<Vector> uvs;
	std::vector<Vector> vertexcolors; // Colors between 0 and 1
	
};

class Sphere : public Geometry {
public:
    double radius;
    Vector albedo;
    explicit Sphere(Vector o, double R, Vector c, double refr = -1, Vector (*m)(double) = &constant_position){
        origin = o;
        radius = R;
        albedo = c;
        movement = m;
        refraction = refr;
    }
    Cast intersect(Ray &r, double time) override {
        Vector origint = origin + (*movement)(time);
        Vector omc = r.origin - origint;
        double delta = pow(dot(r.unit, omc), 2) - (dot(omc, omc) - pow(radius, 2));
        if (delta<0){
            return Cast(Intersection(false, r.origin, 0, false, Vector(0,0,1)), albedo, refraction);
        }
        double sq_delta = sqrt(delta);
        double t = dot(r.unit, origint-r.origin) - sq_delta;
        bool inside = false;
        if (t<0){
            t += 2*sq_delta;
            if (t<0){
                return Cast(Intersection(false, r.origin, 0, false, Vector(0,0,1)), albedo, refraction);
            }
            inside = true;
        }
        Vector normal = (r.origin + r.unit*t) - origint;
        normal.normalize();
        if (inside == true){normal = -normal;}
        return Cast(Intersection(true, r.origin + r.unit*t, t, inside, normal), albedo, refraction);
    }
};

Cast scene_intersect(std::vector<Geometry*> &scene, Ray &r, double t){
    if (scene.size() == 0){
        return Cast(Intersection(false, r.origin, 0, false, Vector(0,0,1)), Vector(0,0,0), -1);
    }
    Cast best = scene[0]->intersect(r, t);
    for (size_t i=1; i<scene.size(); i++){
        Cast current_cast = scene[i]->intersect(r, t);
        if (best.intersect.flag == false || (current_cast.intersect.flag == true && current_cast.intersect.t < best.intersect.t)){
            best = current_cast;
        }
    }
    return best;
}

Ray pixel_ray(int W, int H, int i, int j){
    long double alpha = PI * 60/180;
    Vector u = Vector(j-(W/2)+0.5, (H/2)-i-0.5, -W/(2*tan(alpha/2)));
    return Ray(Vector(0, 0, 0), u);
}

struct Light{
    Vector position;
    int intensity;
};

void place_camera_scene(std::vector<Geometry*> &scene, std::vector<Light> &lights, Vector camera_pos){
    for (size_t i=0; i<scene.size(); i++){
        scene[i]->origin = scene[i]->origin - camera_pos;
    }
    for (size_t i=0; i<lights.size(); i++){
        lights[i].position = lights[i].position - camera_pos;
    }
}

void gamma_correction(Vector& color){
    color[0] = std::min((double)255, std::max((double)0, pow(color[0], 1/2.2)));
    color[1] = std::min((double)255, std::max((double)0, pow(color[1], 1/2.2)));
    color[2] = std::min((double)255, std::max((double)0, pow(color[2], 1/2.2)));
}

Vector random_cos(const Vector &N, const double r1i, const double r2i){
    double r1;
    double r2;
    if (r1i == -1){
        std::hash<std::thread::id> hasher;
        static thread_local std::mt19937 generator = std::mt19937(clock() + hasher(std::this_thread::get_id()));
        std::uniform_real_distribution<double> udis(0.00001,0.99999);
        r1 = udis(generator);
        r2 = udis(generator);
    }
    else{
        r1 = r1i;
        r2 = r2i;
    }
    
    double x = cos(2*PI*r1) * sqrt(1 - r2);
    double y = sin(2*PI*r1) * sqrt(1 - r2);
    double z = sqrt(r2);

    Vector T1;
    if (abs(N.data[0]) <= abs(N.data[1]) && abs(N.data[0]) <= abs(N.data[2])){
        T1 = Vector(0, -N.data[2], N.data[1]);
    }
    else if (abs(N.data[1]) <= abs(N.data[2])){
        T1 = Vector(-N.data[2], 0, N.data[0]);
    }
    else{
        T1 = Vector(-N.data[1], N.data[0], 0);
    }
    T1.normalize();
    Vector T2 = cross(N, T1);
    T2.normalize();

    Vector V = x*T1 + y*T2 + z*N;
    V.normalize();

    return V;
}

Vector normalized_product_element_wise(Vector a, Vector b){
    return Vector(a.data[0] * b.data[0]/255, a.data[1] * b.data[1]/255, a.data[2] * b.data[2]/255);
}

Vector get_color_aux(std::vector<Geometry*> &Scene, std::vector<Light> &Lights, Ray pr, unsigned char reflections_depth, int ray_depth, double r1i, double r2i, double t){
    /*
        Only follows one path, has to be sampled multiple times to get good results
    */
    Vector color = Vector(0,0,0);
    if (ray_depth < 0){return color;} // Should not happen but we never know
    Cast cast = scene_intersect(Scene, pr, t);
    double epsilon = 1.0/100000;
    if (cast.intersect.flag == true){
        Vector normal_towards_ray = cast.intersect.normal;

        double dotwin = dot(pr.unit, normal_towards_ray);
        Vector epsilon_above = cast.intersect.position + normal_towards_ray * epsilon;
        if (cast.mirror && (reflections_depth>0)){
            return get_color_aux(Scene, Lights, Ray(epsilon_above, pr.unit - 2 * dotwin * normal_towards_ray), reflections_depth-1, ray_depth, r1i, r2i, t);
        }
        else if (cast.transp){
            // We always assume the sphere is standing in air
            // We add fresnel; if we have to reflect, then do as if it was a mirror; otherwise do normal
            double n1 = 1.0;
            double n2 = cast.refraction;
            if (cast.intersect.inside == true){
                n1 = cast.refraction;
                n2 = 1;
            }
            double k0 = pow((n1 - n2), 2) / pow(n1 + n2, 2);
            double refl_proba = k0 + (1-k0)*pow(1 - abs(dotwin), 5);
            std::hash<std::thread::id> hasher;
            static thread_local std::mt19937 generator = std::mt19937(clock() + hasher(std::this_thread::get_id()));
            std::uniform_real_distribution<double> udis(0,1);
            if (udis(generator) < refl_proba){
                // If we actually have reflection, reflect
                return get_color_aux(Scene, Lights, Ray(epsilon_above, pr.unit - 2 * dotwin * normal_towards_ray), reflections_depth-1, ray_depth, r1i, r2i, t);
            }
            // End of fresnel
            double n1n2 = n1/n2;
            Vector epsilon_after = cast.intersect.position - normal_towards_ray * epsilon;
            Vector tangential_dir = n1n2 * (pr.unit - dotwin * normal_towards_ray);
            double in_sqrt = 1 - (pow(n1n2,2) * (1 - pow(dotwin,2)));
            if (in_sqrt<0){
                std::cout << "WARNING: issue in refraction handling; transparent surface with mirror behaviour from value of refraction index" << std::endl; // thinks it's inside when it's not
                // This appears when we put the camera inside the lens, for some reason it bugs
                cast.mirror = true;
                cast.refraction = 0;
                cast.transp = false;
                return get_color_aux(Scene, Lights, pr, reflections_depth, ray_depth, r1i, r2i, t);
            }
            Vector normal_dir = - normal_towards_ray * sqrt(in_sqrt);
            Vector refracted_direction = tangential_dir + normal_dir;
            Ray reflected_ray = Ray(epsilon_after, refracted_direction);
            return get_color_aux(Scene, Lights, reflected_ray, reflections_depth-1, ray_depth, r1i, r2i, t);
        }
        Vector albedo = cast.albedo;

        for (size_t k=0; k<Lights.size(); k++){
            // First test if there is a shadow
            Vector to_shadow = Lights[k].position - epsilon_above;
            Ray shadow_ray = Ray(epsilon_above, to_shadow);
            Cast shadow_intersection = scene_intersect(Scene, shadow_ray, t);
            if (!shadow_intersection.intersect.flag | (to_shadow.norm2() < (epsilon_above - shadow_intersection.intersect.position).norm2())){
                // Then add the light to the pixel
                Vector to_light = Lights[k].position - cast.intersect.position;
                color = color + ((Lights[k].intensity/(4*PI*(to_light).norm2())) * (albedo/PI) * std::max((double)0, dot(normal_towards_ray, to_light/to_light.norm())));
            }
        }

        // We add indirect lighting
        if (ray_depth > 0){
            Ray diffuse_bounce = Ray(epsilon_above, random_cos(normal_towards_ray, r1i, r2i));
            color = color + normalized_product_element_wise(albedo, get_color_aux(Scene, Lights, diffuse_bounce, reflections_depth, ray_depth-1, -1, -1, t));
        }
    }
    return color;
}

Vector get_color(std::vector<Geometry*> &Scene, std::vector<Light> &Lights, int W, int H, int ir, int jr, std::mt19937 *generator, unsigned char reflections_depth = 20, int ray_depth = 0, int monte_carlo_size = 2, double DOF_dist = 55, double DOF_radius = 0.75){
    Vector color = Vector(0,0,0);
    std::vector<double> r1v(monte_carlo_size);
    std::vector<double> r2v(monte_carlo_size);
    std::uniform_real_distribution<double> udis(0.00001,0.99999);
    double r1, r2;
    //std::uniform_real_distribution<double> r2dis(std::min((double)j/size_side, 0.00001),std::min((double)(j+1)/size_side, 0.99999));
    int size_side = sqrt(monte_carlo_size);
    for (double i = 0; i<size_side; i++){
        for (double j = 0; j<size_side; j++){
            r1 = udis(*generator);
            r2 = udis(*generator);
            r1v[i*size_side + j] = r1*i/size_side + (1-r1)*(i+1)/size_side;
            r2v[i*size_side + j] = r2*j/size_side + (1-r2)*(j+1)/size_side;
        }
    }
    
    for (int i = pow(size_side, 2); i<monte_carlo_size; i++){
        r1v[i] = udis(*generator);
        r2v[i] = udis(*generator);
    }
    double stdev = 0.4; //between 0.25 and 0.5 should be good for 256*256
    double di, dj, r, theta, t;
    std::uniform_real_distribution<double> r_squared(0, pow(DOF_radius, 2));
    std::uniform_real_distribution<double> theta_gen(0, 2*PI);
    std::uniform_real_distribution<double> t_gen(0, 1);
    Vector P;
    for (int i=0; i<monte_carlo_size; i++){
        r1 = udis(*generator);
        r2 = udis(*generator);
        di = stdev * sqrt(-2*log(r1)) * cos(2*PI*r2);
        dj = stdev * sqrt(-2*log(r1)) * sin(2*PI*r2);
        Ray pr = pixel_ray(W, H, ir+di, jr+dj);
        if (DOF_dist > 0){
            P = pr.origin + pr.unit * DOF_dist/abs(pr.unit.data[2]);
            r = sqrt(r_squared(*generator));
            theta = theta_gen(*generator);
            pr.origin = pr.origin + Vector(r*cos(theta), r*sin(theta), 0);
            pr.unit = P - pr.origin;
            pr.unit.normalize();
        }
        t = t_gen(*generator);
        color = color + get_color_aux(Scene, Lights, pr, reflections_depth, ray_depth, r1v[i], r2v[i], t);
    }
    return color/monte_carlo_size;
}

void concurrent_line(std::vector<Geometry*> Scene, std::vector<Light> Lights, int W, int H, int i0, size_t block_size, std::vector<unsigned char> &image){
    for (size_t i = i0; i < i0+block_size; i++){
        for (int j = 0; j < W; j++) {
            std::hash<std::thread::id> hasher;
            static thread_local std::mt19937 generator = std::mt19937(clock() + hasher(std::this_thread::get_id()));
            Vector color = get_color(Scene, Lights, W, H, i, j, &generator);


            gamma_correction(color);
            image[(i * W + j) * 3 + 0] = color.data[0];
            image[(i * W + j) * 3 + 1] = color.data[1];
            image[(i * W + j) * 3 + 2] = color.data[2];
        }
    }
}

int main(){
    Vector empty_vec = Vector(-1, -1, -1);
    // The SHUTTER TIME for motion blur is always 1 (so movement between t=0 and t=1)
    std::vector<Geometry*> Scene{//new Sphere(Vector(0,-3,0), 5, Vector(170, 10, 170)),        // center ball
                                new Sphere(Vector(0, 1000, 0), 940, Vector(255, 0, 0)),     // top red
                                new Sphere(Vector(0, 0, -1000), 940, Vector(0, 255, 0)),    // end green
                                new Sphere(Vector(0, -1000, 0), 990, Vector(0, 0, 255)),    // bottom blue
                                new Sphere(Vector(0, 0, 1000), 940, Vector(132, 46, 27)),   // back brown
                                new Sphere(Vector(1000, 0, 0), 940, Vector(255, 0, 255)),   // right pink
                                new Sphere(Vector(-1000, 0, 0), 940, Vector(255, 255, 0)),  // left orange
                                //new Sphere(Vector(12, 15, -5), 3, Vector(64, 224, 208), -1, &ninja_movement_yellow),      // small turquoise (ninja)
                                //new Sphere(Vector(15, -2, 0), 3, Vector(64, 224, 208), -1, &throw_movement),      // small turquoise (throw)
                                new Sphere(Vector(-20, 21, -13), 10, empty_vec, 0),         // left mirror
                                new Sphere(Vector(-10, 0, 15), 5, empty_vec, 1.49),         // left lens
                                new TriangleMesh("cat.obj", Vector(0, -10, 0), 0.6)
                                };
    std::vector<Light> Lights{  {Vector(-10, 20, 40), 5*10000000},
                                {Vector(15, 0, -5), 4*1000000}
                                };
    

    place_camera_scene(Scene, Lights, Vector(0, 0, 55));

    int W = 512;
    int H = 512;
 
    std::vector<unsigned char> image(W * H * 3, 0);
    size_t n_threads = 32;
    size_t block_size = H / n_threads;
    std::vector<std::thread> threads(n_threads-1);
    
    for (size_t i = 0; i < n_threads-1; i++) {
        threads[i] = std::thread(&concurrent_line, Scene, Lights, W, H, i*block_size, block_size, std::ref(image));
    }

    for (int i = (n_threads-1)*block_size; i < W; i++){
        for (int j = 0; j < W; j++) {
            std::hash<std::thread::id> hasher;
            static thread_local std::mt19937 generator = std::mt19937(clock() + hasher(std::this_thread::get_id()));
            Vector color = get_color(Scene, Lights, W, H, i, j, &generator);

            gamma_correction(color);
            image[(i * W + j) * 3 + 0] = color.data[0];
            image[(i * W + j) * 3 + 1] = color.data[1];
            image[(i * W + j) * 3 + 2] = color.data[2];
        }
    }

    for (size_t i = 0; i < n_threads-1; i++){
        std::cout << "thread " << i << " joining" << std::endl;
        threads[i].join();
    }

    std::cout << "all threads joined" << std::endl;
    stbi_write_png("image.png", W, H, 3, &image[0], 0);

    for (size_t i = 0; i<Scene.size(); i++){
        delete Scene[i];
    }
 
    return 0;
}