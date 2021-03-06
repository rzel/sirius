// implementation of the "harris hessian" keypoint detector

#include <assert.h>
#include <math.h>
#include <string.h>
#include "xmalloc.c"

// ~ 9*w*h multiplications
void poor_man_gaussian_filter(float *out, float *in, int w, int h, float sigma)
{
	// build 3x3 approximation of gaussian kernel
	float k0 = 1;
	float k1 = exp(-1/(2*sigma*sigma));
	float k2 = exp(-2/(2*sigma*sigma));
	float kn = k0 + 4*k1 + 4*k2;
	float k[3][3] = {{k2, k1, k2}, {k1, k0, k1}, {k2, k1, k2}};

	// hand-made convolution
	for (int j = 1; j < h - 1; j ++)
	for (int i = 1; i < w - 1; i ++)
	{
		float ax = 0;
		for (int dj = 0; dj < 3; dj++)
		for (int di = 0; di < 3; di++)
		{
			int ii = i + di - 1;
			int jj = j + dj - 1;
			ax += k[dj][di] * in[ii+jj*w];
		}
		out[i+j*w] = ax / kn;
	}
}

void rich_man_gaussian_filter(float *out, float *in, int w, int h, float sigma)
{
	// build 5x5 approximation of gaussian kernel
	float k0 = 1;
	float k1 = exp(-1/(2*sigma*sigma));
	float k2 = exp(-4/(2*sigma*sigma));
	float kd = exp(-2/(2*sigma*sigma));
	float kD = exp(-8/(2*sigma*sigma));
	float kh = exp(-5/(2*sigma*sigma));
	float kn = k0 + 4*k1 + 4*k2 + 4*kd + 4*kD + 8*kh;
	float k[5][5] = {
		{kD, kh, k2, kh, kD},
		{kh, kd, k1, kd, kh},
		{k2, k1, k0, k1, k2},
		{kh, kd, k1, kd, kh},
		{kD, kh, k2, kh, kD},
	};

	// hand-made convolution
	for (int j = 2; j < h - 2; j ++)
	for (int i = 2; i < w - 2; i ++)
	{
		float ax = 0;
		for (int dj = 0; dj < 5; dj++)
		for (int di = 0; di < 5; di++)
		{
			int ii = i + di - 2;
			int jj = j + dj - 2;
			ax += k[dj][di] * in[ii+jj*w];
		}
		out[i+j*w] = ax / kn;
	}
}

void fancy_man_gaussian_filter(float *out, float *in, int w, int h, float sigma)
{
	// build 5x5 approximation of gaussian kernel
	float k0 = 1;
	float k1 = exp(-1/(2*sigma*sigma));
	float k2 = exp(-4/(2*sigma*sigma));
	float kd = exp(-2/(2*sigma*sigma));
	float kD = exp(-8/(2*sigma*sigma));
	float kh = exp(-5/(2*sigma*sigma));
	float kn = k0 + 4*k1 + 4*k2 + 4*kd + 4*kD + 8*kh;
	float k[5][5] = {
		{kD, kh, k2, kh, kD},
		{kh, kd, k1, kd, kh},
		{k2, k1, k0, k1, k2},
		{kh, kd, k1, kd, kh},
		{kD, kh, k2, kh, kD},
	};

	// hand-made convolution
	for (int j = 2; j < h - 2; j ++)
	for (int i = 2; i < w - 2; i ++)
	{
		float ax = 0;
		for (int dj = 0; dj < 5; dj++)
		for (int di = 0; di < 5; di++)
		{
			int ii = i + di - 2;
			int jj = j + dj - 2;
			ax += k[dj][di] * in[ii+jj*w];
		}
		out[i+j*w] = ax / kn;
	}
}

// the type of a "getpixel" function
typedef float (*getpixel_operator)(float*,int,int,int,int);

// extrapolate by nearest value (useful for Neumann boundary conditions)
static float getpixel_1(float *I, int w, int h, int i, int j)
{
	if (i < 0) i = 0;
	if (j < 0) j = 0;
	if (i >= w) i = w-1;
	if (j >= h) j = h-1;
	return I[i+j*w];
}

static float getlaplacian_1(float *I, int w, int h, int i, int j)
{
	return -4 * getpixel_1(I, w, h, i  , j  )
	          + getpixel_1(I, w, h, i+1, j  )
	          + getpixel_1(I, w, h, i  , j+1)
	          + getpixel_1(I, w, h, i-1, j  )
	          + getpixel_1(I, w, h, i  , j-1);
}

// 0 multiplications (ideally)
static void downsample_by_factor_two(float *out, int ow, int oh,
		float *in, int iw, int ih)
{
	if (!out || !in) return;
	getpixel_operator p = getpixel_1;
	assert(abs(2*ow-iw) < 2);
	assert(abs(2*oh-ih) < 2);
	for (int j = 0; j < oh; j++)
	for (int i = 0; i < ow; i++)
		out[ow*j+i] = p(in, iw, ih, 2*i, 2*j);
}

#define MAX_LEVELS 20
struct gray_image_pyramid {
	int n;                // number of levels
	int w[MAX_LEVELS];    // width of each level
	int h[MAX_LEVELS];    // height of each level
	float *x[MAX_LEVELS]; // data of each level
};

float pyramidal_laplacian(struct gray_image_pyramid *p, float x, float y, int o)
{
	if (o < 0 || o >= p->n)
		return -INFINITY;
	float *I = p->x[o];
	int w = p->w[o];
	int h = p->h[o];
	float a00 = getlaplacian_1(I, w, h, round(x+0), round(y+0));
	//return a00;
	float a10 = getlaplacian_1(I, w, h, round(x+1), round(y+0));
	float a01 = getlaplacian_1(I, w, h, round(x+0), round(y+1));
	float am0 = getlaplacian_1(I, w, h, round(x-1), round(y+0));
	float a0m = getlaplacian_1(I, w, h, round(x+0), round(y-1));
	return (4*a00+a10+a01+am0+a0m)/8;//fmax(a00, fmax(fmax(a10,a01),fmax(am0,a0m)));
	//return fmax(a00, fmax(fmax(a10,a01),fmax(am0,a0m)));
}

//#include <math.h>
//float pyr_trilinear(struct gray_image_pyramid *p, float x, float y, float s)
//{
//	int o0 = floor(log2(s));
//	int o1 = ceil(log2(s));
//	int i = floor(x);
//	int j = floor(y);
//	int k = floor(z);
//	float X = x - i;
//	float Y = y - j;
//	float Z = z - k;
//	float v000 = p(im,i,j,k);//im->t[k  ][j  ][i  ];
//	float v001 = p(im,i,j,k+1);//im->t[k+1][j  ][i  ];
//	float v010 = p(im,i,j+1,k);//im->t[k  ][j+1][i  ];
//	float v100 = p(im,i+1,j,k);//im->t[k  ][j  ][i+1];
//	float v110 = p(im,i+1,j+1,k);//im->t[k+1][j+1][i  ];
//	float v101 = p(im,i+1,j,k+1);//im->t[k+1][j  ][i+1];
//	float v011 = p(im,i,j+1,k+1);//im->t[k  ][j+1][i+1];
//	float v111 = p(im,i+1,j+1,k+1);//im->t[k+1][j+1][i+1];
//	float r = v000*(1-X)*(1-Y)*(1-Z)
//		+  v100*(X)*(1-Y)*(1-Z)
//		+  v010*(1-X)*(Y)*(1-Z)
//		+  v001*(1-X)*(1-Y)*(Z)
//		+  v011*(1-X)*(Y)*(Z)
//		+  v101*(X)*(1-Y)*(Z)
//		+  v110*(X)*(Y)*(1-Z)
//		+  v111*X*Y*Z;
//	return r;
//}

// ~ 18*w*h multiplications
static void fill_pyramid(struct gray_image_pyramid *p, float *x, int w, int h,
		float S)
{
	//float S = 2.8 / 2; // magic value! do not change
	if (S < 0) {
		int i = 0;
		p->w[i] = w;
		p->h[i] = h;
		p->x[i] = xmalloc_float(w * h);
		while(1) {
			i += 1;
			if (i + 1 >= MAX_LEVELS) break;
			p->w[i] = ceil(p->w[i-1]/2);
			p->h[i] = ceil(p->h[i-1]/2);
			if (p->w[i] <= 1 && p->h[i] <= 1) break;
			p->x[i] = xmalloc_float(p->w[i] * p->h[i]);
			p->n = i;
		}
		for (int i = 0; i < p->n; i++)
		for (int j = 0; j < p->w[i] * p->h[i]; j++)
			p->x[i][j] = 0;
		return;
	}

	int i = 0;
	p->w[i] = w;
	p->h[i] = h;
	p->x[i] = xmalloc_float(w * h);
	memcpy(p->x[i], x, w*h*sizeof*x);
	while (1) {
		i += 1;
		if (i + 1 >= MAX_LEVELS) break;
		p->w[i] = ceil(p->w[i-1]/2);
		p->h[i] = ceil(p->h[i-1]/2);
		if (p->w[i] <= 1 && p->h[i] <= 1) break;
		p->x[i] = xmalloc_float(p->w[i] * p->h[i]);
		float *tmp1 = xmalloc_float(p->w[i-1] * p->h[i-1]);
		//float *tmp2 = xmalloc_float(p->w[i-1] * p->h[i-1]);
		//float *tmp3 = xmalloc_float(p->w[i-1] * p->h[i-1]);
		poor_man_gaussian_filter(tmp1,p->x[i-1],p->w[i-1],p->h[i-1], S);
		//poor_man_gaussian_filter(tmp2,tmp1,p->w[i-1],p->h[i-1], S);
		//poor_man_gaussian_filter(tmp3,tmp2,p->w[i-1],p->h[i-1], S);
		downsample_by_factor_two(p->x[i], p->w[i], p->h[i],
			       	tmp1, p->w[i-1], p->h[i-1]);
		free(tmp1);
		//free(tmp2);
		//free(tmp3);
	}
	p->n = i;
}


static void free_pyramid(struct gray_image_pyramid *p)
{
	for (int i = 0; i < p->n; i++)
		free(p->x[i]);
}

static float parabolic_minimum(float p, float q, float r)
{
	//return 0;
	if (isfinite(p) && isfinite(q) && isfinite(r))
	{
		//if (p < q && p < r) return -1;
		//if (q < p && q < r) return 0;
		//if (r < p && r < q) return 1;
		float alpha = (p + r) / 2 - q;
		float beta  = (p - r) / 2;
		float R = 0.5 * beta / alpha;
		if (fabs(R) > 0.5) {
			if (p < r) return -1;
			return 1;
		}
		assert(fabs(R) <= 0.5);
		return R;
	}
	if ( isfinite(p) &&  isfinite(q) && !isfinite(r)) return p < q ? -1 : 0;
	if (!isfinite(p) &&  isfinite(q) &&  isfinite(r)) return q < r ? 0 : 1;
	if (!isfinite(p) &&  isfinite(q) && !isfinite(r)) return 0;
	if ( isfinite(p) && !isfinite(q) && !isfinite(r)) return -1;
	if (!isfinite(p) && !isfinite(q) &&  isfinite(r)) return 1;
	if (!isfinite(p) && !isfinite(q) && !isfinite(r)) return NAN;
	fprintf(stderr, "(%g %g %g)", p, q, r);
	return NAN;
}

//#include <math.h>
//static
//float harressian_score_at(float *x, int w, int h, int i, int j,
//		float kappa, float tau)
//{
//	if (i < 1 || j < 1 || i > w-2 || j > h-2)
//		return -INFINITY;
//	float sign = kappa > 0 ? 1 : -1;
//	kappa = fabs(kappa);
//	float Vmm = sign * x[(i-1) + (j-1)*w];
//	float V0m = sign * x[(i+0) + (j-1)*w];
//	float Vpm = sign * x[(i+1) + (j-1)*w];
//	float Vm0 = sign * x[(i-1) + (j+0)*w];
//	float V00 = sign * x[(i+0) + (j+0)*w];
//	float Vp0 = sign * x[(i+1) + (j+0)*w];
//	float Vmp = sign * x[(i-1) + (j+1)*w];
//	float V0p = sign * x[(i+0) + (j+1)*w];
//	float Vpp = sign * x[(i+1) + (j+1)*w];
//	if (V0m<V00 || Vm0<V00 || V0p<V00 || Vp0<V00
//			|| Vmm<V00 || Vpp<V00 || Vmp<V00 || Vpm<V00)
//		return -INFINITY;
//	float dxx = Vm0 - 2*V00 + Vp0;
//	float dyy = V0m - 2*V00 + V0p;
//	float dxy =  (Vpp + Vmm - Vpm - Vmp)/4;
//	float dyx = -(Vpm + Vmp - Vpp - Vmm)/4;
//	float T = dxx + dyy;
//	float D = dxx * dyy - dxy * dyx;
//	float R0 = D - kappa * T * T;
//	if (T > tau && R0 > 0)
//		return T;
//	else
//		return -INFINITY;
//}

//static float pyr_harressian_score(struct gray_image_pyramid *p,
//		int i, int j, int o, float kappa, float tau)
//{
//	if (o < 0 || o >= p->n)
//		return -INFINITY;
//	return harressian_score_at(p->x[o], p->w[o], p->h[o], i, j, kappa, tau);
//}


// ~ 9*w*h multiplications
int harressian_nogauss(float *out_xyt, int max_npoints,
		float *x, int w, int h, float kappa, float tau)
{
	float sign = kappa > 0 ? 1 : -1;
	kappa = fabs(kappa);
	int n = 0;
	for (int j = 2; j < h - 2; j++)
	for (int i = 2; i < w - 2; i++)
	{
		// Vmm V0m Vpm
		// Vm0 V00 Vp0
		// Vmp V0p Vpp
		float Vmm = sign * x[(i-1) + (j-1)*w];
		float V0m = sign * x[(i+0) + (j-1)*w];
		float Vpm = sign * x[(i+1) + (j-1)*w];
		float Vm0 = sign * x[(i-1) + (j+0)*w];
		float V00 = sign * x[(i+0) + (j+0)*w];
		float Vp0 = sign * x[(i+1) + (j+0)*w];
		float Vmp = sign * x[(i-1) + (j+1)*w];
		float V0p = sign * x[(i+0) + (j+1)*w];
		float Vpp = sign * x[(i+1) + (j+1)*w];
		if (V0m<V00 || Vm0<V00 || V0p<V00 || Vp0<V00
				|| Vmm<V00 || Vpp<V00 || Vmp<V00 || Vpm<V00)
			continue;
		float dxx = Vm0 - 2*V00 + Vp0;
		float dyy = V0m - 2*V00 + V0p;
		float dxy =  (Vpp + Vmm - Vpm - Vmp)/4;
		float dyx = -(Vpm + Vmp - Vpp - Vmm)/4;
		//float dxy =  (2*V00 +Vpm +Vmp -Vm0 -V0m -Vp0 -V0p)/2;
		//float dyx = -(2*V00 +Vmm +Vpp -Vm0 -V0p -Vp0 -V0m)/2;
		//
		// XXX TODO : these schemes for dxy have directional aliasing!
		//
		float T = dxx + dyy;
		float D = dxx * dyy - dxy * dyx;
		float R0 = D - kappa * T * T;
		if (T > tau && R0 > 0)
		{
			// TODO: higher-order sub-pixel localisation
			out_xyt[3*n+0] = i + parabolic_minimum(Vm0, V00, Vp0);
			out_xyt[3*n+1] = j + parabolic_minimum(V0m, V00, V0p);
			out_xyt[3*n+2] = T;
			n += 1;
		}
		if (n >= max_npoints - 1)
			goto done;
	}
done:
	assert(n < max_npoints);
	return n;
}

//static float evaluate_bilinear_cell(float a, float b, float c, float d,
//							float x, float y)
//{
//	float r = 0;
//	r += a * (1-x) * (1-y);
//	r += b * ( x ) * (1-y);
//	r += c * (1-x) * ( y );
//	r += d * ( x ) * ( y );
//	return r;
//}
//
//static float bilinear_interpolation_at(float *x, int w, int h, float p, float q)
//{
//	int ip = p;
//	int iq = q;
//	float a = getpixel_1(x, w, h, ip  , iq  );
//	float b = getpixel_1(x, w, h, ip+1, iq  );
//	float c = getpixel_1(x, w, h, ip  , iq+1);
//	float d = getpixel_1(x, w, h, ip+1, iq+1);
//	float r = evaluate_bilinear_cell(a, b, c, d, p-ip, q-iq);
//	return r;
//}
//
//// extrapolate by 0
//static float getpixel_0(float *x, int w, int h, int i, int j)
//{
//	if (i < 0 || i >= w || j < 0 || j >= h)
//		return 0;
//	return x[i+j*w];
//}
//
//static float cubic_interpolation(float v[4], float x)
//{
//	return v[1] + 0.5 * x*(v[2] - v[0]
//			+ x*(2.0*v[0] - 5.0*v[1] + 4.0*v[2] - v[3]
//			+ x*(3.0*(v[1] - v[2]) + v[3] - v[0])));
//
//	float y = 3.0*(v[1] - v[2]) + v[3] - v[0];
//	y = x*y + 2.0*v[0] - 5.0*v[1] + 4.0*v[2] - v[3];
//	y = x*y + v[2] - v[0];
//	y = 0.5*x*y + v[1];
//	return y;
//}
//
//static float bicubic_interpolation_cell(float p[4][4], float x, float y)
//{
//	float v[4];
//	v[0] = cubic_interpolation(p[0], y);
//	v[1] = cubic_interpolation(p[1], y);
//	v[2] = cubic_interpolation(p[2], y);
//	v[3] = cubic_interpolation(p[3], y);
//	return cubic_interpolation(v, x);
//}
//
//float bicubic_interpolation_gray(float *img, int w, int h, float x, float y)
//{
//	x -= 1;
//	y -= 1;
//
//	getpixel_operator p = getpixel_0;
//
//	int ix = floor(x);
//	int iy = floor(y);
//	float c[4][4];
//	for (int j = 0; j < 4; j++)
//		for (int i = 0; i < 4; i++)
//			c[i][j] = p(img, w, h, ix + i, iy + j);
//	float r = bicubic_interpolation_cell(c, x - ix, y - iy);
//	return r;
//}

float nn_interpolation_at(float *img, int w, int h, float x, float y)
{
	int ix = round(x);
	int iy = round(y);
	return getpixel_1(img, w, h, ix, iy);
}

static uint8_t float_to_byte(float x)
{
	int r = round(x);
	if (r < 0) return 0;
	if (r > 255) return 255;
	return r;
}

static void mini_filtering_inplace(float *x, int w, int h, float kappa, float tau)
{
	float *tmp = xmalloc(w*h*sizeof*tmp);
	for (int i = 0; i < w*h; i++) tmp[i] = 0;
	float sign = kappa > 0 ? 1 : -1;
	kappa = fabs(kappa);
	fprintf(stderr, "tau = %g\n", tau);
	for (int j = 2; j < h - 2; j++)
	for (int i = 2; i < w - 2; i++)
	{
		// Vmm V0m Vpm
		// Vm0 V00 Vp0
		// Vmp V0p Vpp
		//float Vmm = sign * x[(i-1) + (j-1)*w];
		float V0m = sign * x[(i+0) + (j-1)*w];
		//float Vpm = sign * x[(i+1) + (j-1)*w];
		float Vm0 = sign * x[(i-1) + (j+0)*w];
		float V00 = sign * x[(i+0) + (j+0)*w];
		float Vp0 = sign * x[(i+1) + (j+0)*w];
		//float Vmp = sign * x[(i-1) + (j+1)*w];
		float V0p = sign * x[(i+0) + (j+1)*w];
		//float Vpp = sign * x[(i+1) + (j+1)*w];
		float dxx = Vm0 - 2*V00 + Vp0;
		float dyy = V0m - 2*V00 + V0p;
		//float dxy =  (Vpp + Vmm - Vpm - Vmp)/4;
		//float dyx = -(Vpm + Vmp - Vpp - Vmm)/4;
		float T = dxx + dyy;
		//float D = dxx * dyy - dxy * dyx;
		//float R = D / (T*T);
		tmp[j*w+i] = float_to_byte(127+tau*T);
	}

	for (int i = 0; i < w*h; i++)
		x[i] = tmp[i];
	free(tmp);
}


void fill_pyramid_level(float *inout, int w, int h,
		float sigma_pre, float sigma_pyr, int octave,
		float kappa, float tau)
{
	// filter input image
	float *sx = xmalloc_float(w * h);
	poor_man_gaussian_filter(sx, inout, w, h, sigma_pre);

	// create image pyramid
	struct gray_image_pyramid p[1];
	fill_pyramid(p, sx, w, h, sigma_pyr);

	// evaluate image at the requested octave
	if (octave < 0) octave = 0;
	if (octave >= p->n) octave = p->n-1;
	float Z = 1 << octave;
	float *x = p->x[octave];
	int wx = p->w[octave];
	int hx = p->h[octave];

	mini_filtering_inplace(x, wx, hx, kappa, tau);

	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
		sx[j*w+i] = nn_interpolation_at(x, wx, hx, i/Z, j/Z);
		//gray[j*w+i] = bilinear_interpolation_at(x, wx, hx, i/Z, j/Z);
		//gray[j*w+i] = bicubic_interpolation_gray(x, wx, hx, i/Z, j/Z);

	for (int i = 0; i < h*w; i++)
		inout[i] = sx[i];

	// cleanup and exit
	free_pyramid(p);
	free(sx);
}

int harressian_ms(float *out_xyst, int max_npoints, float *x, int w, int h,
		float sigma, float kappa, float tau)
{
	// filter input image
	float *sx = xmalloc_float(w * h);
	poor_man_gaussian_filter(sx, x, w, h, sigma);

	// create image pyramid
	struct gray_image_pyramid p[1];
	fill_pyramid(p, sx, w, h, 2.8/2);
	float *tab_xyt = xmalloc_float(3 * max_npoints);

	// apply nongaussian harressian at each level of the pyramid
	int n = 0;
	for (int l = p->n - 1; l >= 0; l--)
	{
		int n_l = harressian_nogauss(tab_xyt, max_npoints - n,
				p->x[l], p->w[l], p->h[l], kappa, tau);
		float factor = 1 << l;
		for (int i = 0; i < n_l; i++)
		{
			if (n >= max_npoints) break;
			float x = tab_xyt[3*i+0];
			float y = tab_xyt[3*i+1];

			// first-order scale localization
			float A = fabs(pyramidal_laplacian(p, x/2, y/2, l+1));
			float B = fabs(pyramidal_laplacian(p, x, y, l));
			float C = fabs(pyramidal_laplacian(p, x*2, y*2, l-1));
			if (l > 0 && C > B) continue;
			if (A > B) continue;
			//if (l > 0 && A > B) continue;
			//if (A > B || C > B) continue;
			float factor_scaling = 0;//parabolic_minimum(-A, -B, -C);
			float new_factor = factor * (3*factor_scaling + 5) / 4;
			//fprintf(stderr, "(%g %g %g) ", factor, factor_scaling, new_factor);

			out_xyst[4*n+0] = factor * x;
			out_xyst[4*n+1] = factor * y;
			out_xyst[4*n+2] = new_factor;
			out_xyst[4*n+3] = tab_xyt[3*i+2];
			n++;
		}
	}
	assert(n <= max_npoints);

	// cleanup and exit
	free(tab_xyt);
	free_pyramid(p);
	free(sx);
	return n;
}

bool point_is_redundant(float *a, float *b)
{
	float ax = a[0]; float ay = a[1]; float as = a[2];
	float bx = b[0]; float by = b[1]; float bs = b[2];
	float d = hypot(ax - bx, ay - by);
	float s = fmax(as, bs);
	return fabs(log2(as) - log2(bs)) < 3 && d < 1*s;
}

int remove_redundant_points(float *out_xyst, float *in_xyst, int n)
{
	int r = 0;
	for (int i = 0; i < n; i++)
	{
		bool keep_this_i = true;
		for (int j = i+1; j < n; j++) // assume they are ordered by "s"
			if (point_is_redundant(in_xyst + 4*i, in_xyst + 4*j))
				keep_this_i = false;
		if (keep_this_i) {
			for (int l = 0; l < 4; l++)
				out_xyst[4*r+l] = in_xyst[4*i+l];
			r += 1;
		}
	}
	return r;
}

// harressian with multi-scale exclusion
// xyst = (x position, y position, scale, score)
int harressian(float *out_xyst, int max_npoints, float *x, int w, int h,
		float sigma, float kappa, float tau)
{
	float tmp_xyst[4*max_npoints];
	int n = harressian_ms(tmp_xyst, max_npoints, x, w, h, sigma,kappa,tau);
	int r = remove_redundant_points(out_xyst, tmp_xyst, n);
	return r;
}

