//#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "cv.h"
#include "highgui.h"



#include "harris/harris.c"
#include "harris/gauss.c"
#include "harris/image.c"
#include "harris/ntuple.c"
#include "harris/misc.c"

#define OMIT_MAIN_FONTU
#include "fontu.c"
#include "fonts/xfont_9x15.c"
#include "fonts/xfont_8x13.c"

#include "seconds.c"


#include "xmalloc.c"

#define OMIT_MAIN
#include "ransac.c"


#include "geometry.c"


struct bitmap_font global_font;

double global_harris_sigma = 1;    // s
double global_harris_k = 0.04;     // k
double global_harris_flat_th = 20; // t
int    global_harris_neigh = 3;    // n

int    global_ransac_ntrials = 10000; // r
int    global_ransac_minliers = 9;    // i
double global_ransac_maxerr = 1.5;    // e


int find_straight_line_by_ransac(bool *out_mask, float line[3],
		float *points, int npoints,
		int ntrials, float max_err)
{
	return ransac(out_mask, line, points, 2, npoints, 3,
			distance_of_point_to_straight_line,
			straight_line_through_two_points,
			4, ntrials, 3, max_err, NULL, NULL);
}

struct drawing_state {
	int w, h;
	float *color;
	float *frgb;
};

static void plot_frgb_pixel(int i, int j, void *ee)
{
	struct drawing_state *e = ee;
	if (!insideP(e->w, e->h, i, j))
		return;
	e->frgb[3*(j * e->w + i) + 0] = e->color[0];
	e->frgb[3*(j * e->w + i) + 1] = e->color[1];
	e->frgb[3*(j * e->w + i) + 2] = e->color[2];
}

static void draw_segment_frgb(float *frgb, int w, int h,
		int from[2], int to[2], float color[3])
{
	struct drawing_state e = {.w = w, .h = h, .frgb = frgb, .color = color};
	traverse_segment(from[0], from[1], to[0], to[1], plot_frgb_pixel, &e);
}


// process one frame
static void process_tacu(float *out, float *in, int w, int h, int pd)
{
	double framerate = seconds();

	// convert image to gray (and put it into rafa's image structure)
	image_double in_gray = new_image_double(w, h);
	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	{
		int idx = j*w + i;
		float r = in[3*idx+0];
		float g = in[3*idx+1];
		float b = in[3*idx+2];
		in_gray->data[idx] = (r + g + b)/3;
	}

	// fill-in gray values (for visualization)
	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	{
		int idx = j*w + i;
		out[3*idx+0] = in_gray->data[idx];
		out[3*idx+1] = in_gray->data[idx];
		out[3*idx+2] = in_gray->data[idx];
	}

	// computi harris-hessian
	double tic = seconds();
	ntuple_list hp = harris2(in_gray,
			global_harris_sigma,
			global_harris_k,
			global_harris_flat_th,
			global_harris_neigh
			);
	tic = seconds() - tic;
	fprintf(stderr, "harris took %g milliseconds (%g hz)\n",
			tic*1000, 1/tic);

	// plot detected keypoints
	int n[][2] = {
		{0,0},
	       	{-1,0}, {0,-1}, {0,1}, {1,0}, // 5
		{-1,-1}, {-1,+1}, {1,-1}, {1,+1} // 9
	}, nn = 9;

	for (int i = 0; i < hp->size; i++)
	{
		assert(hp->dim == 2);
		int x = hp->values[2*i+0];
		int y = hp->values[2*i+1];
		for (int p = 0; p < nn; p++)
		{
			int xx = x + n[p][0];
			int yy = y + n[p][1];
			int idx = yy*w + xx;
			if (idx < 0 || idx >= w*h) continue;
			out[3*idx + 0] = 0;
			out[3*idx + 1] = 255;
			out[3*idx + 2] = 0;
		}
	}

	// compute ransac
	if (hp->size > 8)
	{
		// data for ransac
		int n = hp->size;
		float *data = xmalloc(2*n * sizeof*data);
		for (int i = 0; i < 2*n; i++)
			data[i] = hp->values[i];
		bool *mask = xmalloc(n * sizeof*mask);

		// find line
		float line[3];
		int n_inliers = find_straight_line_by_ransac(mask, line,
				data, n,
				global_ransac_ntrials, global_ransac_maxerr);

		// plot the line, if found
		if (n_inliers > global_ransac_minliers)
		{
			//printf("RANSAC(%d): %g %g %g\n", n_inliers,
			//		line[0], line[1], line[2]);
			double dline[3] = {line[0], line[1], line[2]};
			double rectangle[4] = {0, 0, w, h};
			double segment[4];
			bool r = cut_line_with_rectangle(segment, segment+2,
					dline, rectangle, rectangle+2);
			if (!r) fprintf(stderr, "WARNING: bad line!\n");
			int ifrom[2] = {round(segment[0]), round(segment[1])};
			int ito[2] = {round(segment[2]), round(segment[3])};
			float fred[3] = {0, 0, 255};
			draw_segment_frgb(out, w, h, ifrom, ito, fred);
		}

		// cleanup
		free(mask);
		free(data);
	}

	free_ntuple_list(hp);
	free_image_double(in_gray);

	// draw HUD
	char buf[1000];
	snprintf(buf, 1000, "sigma = %g\nk=%g\nt=%g\nn=%d",
			global_harris_sigma,
			global_harris_k,
			global_harris_flat_th,
			global_harris_neigh);
	float fg[] = {0, 255, 0};
	put_string_in_float_image(out,w,h,3, 5,5, fg, 0, &global_font, buf);
	snprintf(buf, 1000, "ransac ntrials = %d\nransac minliers = %d\n"
			"ransac maxerr = %g",
			global_ransac_ntrials,
			global_ransac_minliers,
			global_ransac_maxerr);
	put_string_in_float_image(out,w,h,3, 105,5, fg, 0, &global_font, buf);

	framerate = seconds() - framerate;
	snprintf(buf, 1000, "%g Hz", 1/framerate);
	put_string_in_float_image(out,w,h,3, 305,5, fg, 0, &global_font, buf);
}

int main( int argc, char *argv[] )
{
	if (argc != 2)
		return fprintf(stderr, "usage:\n\t%s <cam_id>\n", *argv);
	int cam_id = atoi(argv[1]);

	global_font = *xfont_8x13;
	global_font = reformat_font(global_font, UNPACKED);

	CvCapture *capture = 0;
	int accum_index = 0;
	int       key = 0;

	/* initialize camera */
	capture = cvCaptureFromCAM( cam_id );
	//cvCaptureFromCAM
	//capture.set(CVCAP_IMAGE_WIDTH, 1920);

	/* always check */
	if ( !capture )
		fail("could not get a capture");

	IplImage *frame = cvQueryFrame(capture);
	if (!frame) fail("did not get frame");
	int w = frame->width, W = 512;
	int h = frame->height, H = 512;
	int pd = frame->nChannels;
	int depth = frame->depth;
	fprintf(stderr, "%dx%d %d [%d]\n", w, h, pd, depth);
	if (w != 640 || h != 480 || pd != 3)
		fail("unexpected webcam size, "
				"please change some hard-coded numbers");

	//if (W > w || H > h) fail("bad crop");
	CvSize size;
	size.width = W;
	size.height = H;
	IplImage *frame_small = cvCreateImage(size, depth, pd);
	//IplImage *frame_big = cvCreateImage(size, depth, pd);
	//fprintf(stderr, "%dx%d %d [%d]\n", frame_big->width, frame_big->height, pd, depth);

	float *taccu_in = xmalloc(W*H*pd*sizeof*taccu_in);
	float *taccu_out = xmalloc(W*H*pd*sizeof*taccu_in);
	for (int i = 0; i < W*H; i++) {
		int g = 0;//rand()%0x100;
		taccu_in[3*i+0] = g;
		taccu_in[3*i+1] = g;
		taccu_in[3*i+2] = g;
	}

	/* create a window for the video */
	cvNamedWindow( "result", CV_WINDOW_FREERATIO );
	cvResizeWindow("result", W, H);

	while( key != 'q' ) {
		/* get a frame */
		frame = cvQueryFrame( capture );

		/* always check */
		if( !frame ) break;

		if (frame->width != w) fail("got bad width");
		if (frame->height != h) fail("got bad height");
		if (frame->nChannels != pd) fail("got bad nc");
		if (frame->depth != depth) fail("got bad depth");
		if (pd != 3) fail("pd is not 3");

		//for (int i = 0; i < W * H * pd; i++)
		//{
		//	taccu_in[i] = (float)(unsigned char)frame->imageData[i];
		//}
		for (int j = 0; j < 384; j++)
		for (int i = 0; i < 512; i++)
		for (int l = 0; l < pd; l++)
			taccu_in[((j+64)*512+i)*pd+l] = (float)(unsigned char)
				frame->imageData[((j+48)*w+i+64)*pd+l];

		process_tacu(taccu_out, taccu_in, W, H, pd);

		taccu_out[0]=taccu_out[1]=taccu_out[2]=0;
		taccu_out[3]=taccu_out[4]=taccu_out[5]=255;

		for (int i = 0; i < W * H * pd; i++)
			frame_small->imageData[i] = taccu_out[i];

		cvShowImage( "result", frame_small );

		/* exit if user press 'q' */
		key = cvWaitKey( 1 ) % 0x10000;
		double wheel_factor = 1.1;
		if (key == 's') global_harris_sigma /= wheel_factor;
		if (key == 'S') global_harris_sigma *= wheel_factor;
		if (key == 'k') global_harris_k /= wheel_factor;
		if (key == 'K') global_harris_k *= wheel_factor;
		if (key == 't') global_harris_flat_th /= wheel_factor;
		if (key == 'T') global_harris_flat_th *= wheel_factor;
		if (key == 'n' && global_harris_neigh > 1)
			global_harris_neigh -= 1;
		if (key == 'N') global_harris_neigh += 1;
		if (key == 'r' && global_ransac_ntrials > 10)
			global_ransac_ntrials /= wheel_factor;
		if (key == 'R') global_ransac_ntrials *= wheel_factor;
		if (key == 'i' && global_ransac_minliers > 2)
			global_ransac_minliers -= 1;
		if (key == 'I') global_ransac_minliers += 1;
		if (key == 'e') global_ransac_maxerr /= wheel_factor;
		if (key == 'E') global_ransac_maxerr *= wheel_factor;
		if (isalpha(key)) {
			printf("harris_sigma = %g\n", global_harris_sigma);
			printf("harris_k = %g\n", global_harris_k);
			printf("harris_t = %g\n", global_harris_flat_th);
			printf("harris_n = %d\n", global_harris_neigh);
			printf("\n");
		}
	}

	/* free memory */
	cvDestroyWindow( "result" );
	cvReleaseCapture( &capture );

	return 0;
}
