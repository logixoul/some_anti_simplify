#include "precompiled.h"
//#include "ciextra.h"
#include "util.h"
//#include "shade.h"
#include "stuff.h"
#include "gpgpu.h"
#include "cfg1.h"
#include "sw.h"
#include "gpuBlur2_5.h"
#include "RasterizationRenderer.h"

#include "stefanfw.h"

typedef WrapModes::GetWrapped WrapMode;

// baseline 7fps
// now 9fps

//int wsx=800, wsy=800.0*(800.0/1280.0);
int wsx=768, wsy=768;
//int scale=2;
int sx=256;
int sy=256;
Array2D<float> img(sx,sy);
bool pause2=false;
std::map<int, gl::TextureRef> texs;
auto imgadd_accum = Array2D<float>(sx,sy);

// I have a `restoring_functionality_after_merge` branch where I attempt to merge supportlib from Tonemaster

template<class T, class FetchFunc>
Array2D<T> gauss3_(Array2D<T> src) {
	T zero = ::zero<T>();
	Array2D<T> dst1(src.w, src.h);
	Array2D<T> dst2(src.w, src.h);
	forxy(dst1)
		dst1(p) = .25f * (2 * FetchFunc::fetch(src, p.x, p.y) + FetchFunc::fetch(src, p.x - 1, p.y) + FetchFunc::fetch(src, p.x + 1, p.y));
	forxy(dst2)
		dst2(p) = .25f * (2 * FetchFunc::fetch(dst1, p.x, p.y) + FetchFunc::fetch(dst1, p.x, p.y - 1) + FetchFunc::fetch(dst1, p.x, p.y + 1));
	return dst2;
}

struct SApp : App {
	RasterizationRenderer renderer = RasterizationRenderer(ivec2(wsx, wsy), ivec2(sx, sy));


	void setup()
	{
		createConsole();
		reset();
		enableDenormalFlushToZero();
		setWindowSize(wsx, wsy);

		createConsole();
		disableGLReadClamp();
		stefanfw::eventHandler.subscribeToEvents(*this);


	}

	void update()
	{
		stefanfw::beginFrame();
		stefanUpdate();
		stefanDraw();
		stefanfw::endFrame();
	}
	void keyDown(KeyEvent e)
	{
		if(keys['p'] || keys['2'])
		{
			pause2 = !pause2;
		}
		if(keys['r'])
		{
			reset();
		}
	}
	void reset() {
		forxy(img) {
			img(p)=ci::randFloat();
		}
		forxy(imgadd_accum) {
			imgadd_accum(p)=0.0f;
		}
	}
	
	typedef Array2D<float> Img;
	static Img update_1_scale(Img aImg)
	{
		auto img = aImg.clone();
		auto abc=cfg1::getOpt("abc", 1.0f,
			[&]() { return keys['a']; },
			[&]() { return niceExpRangeX(mouseX, .05f, 1000.0f); });
		auto contrastizeFactor = cfg1::getOpt("contrastizeFactor", 0.0f,
			[&]() { return keys['c']; },
			[&]() { return niceExpRangeX(mouseX, .05f, 1000.0f); });

		auto tex = gtex(img);
		gl::TextureRef gradientsTex;
		sw::timeit("calc velocities [get_gradients]", [&]() {
			gradientsTex = get_gradients_tex(tex);
		});
		sw::timeit("calc velocities [the rest]", [&]() {
			globaldict["abc"] = abc;
			tex = shade2(tex, gradientsTex,
				"vec2 grad = fetch2(tex2);"
				"vec2 dir = perpLeft(safeNormalized(grad));"
				""
				"float val = fetch1();"
				"float valLeft = fetch1(tex, tc + tsize * dir);"
				"float valRight = fetch1(tex, tc - tsize * dir);"
				"float add = (val - (valLeft + valRight) * .5f);"
				"_out.r = val + add * abc;"
				, ShadeOpts(),
				"vec2 perpLeft(vec2 v) {"
				"	return vec2(-v.y, v.x);"
				"}"
			);
		});
		
		sw::timeit("blur", [&]() {
			/*auto imgb = gauss3_<float, WrapModes::GetWrapped>(img);//gaussianBlur(img, 3);
			//img=imgb;
			forxy(img) {
				img(p) = lerp(img(p), imgb(p), .8f);
			}*/
			auto texb = gauss3tex(tex);
			tex = shade2(tex, texb,
				"float f = fetch1();"
				"float fb = fetch1(tex2);"
				"_out.r = mix(f, fb, .8f);"
			);
		});
		img = gettexdata<float>(tex, GL_RED, GL_FLOAT);
		img = ::to01(img);
		sw::timeit("restore avg", [&]() {
			float sum = std::accumulate(img.begin(), img.end(), 0.0f);
			float avg = sum / (float)img.area;
			forxy(img)
			{
				img(p) += .5f - avg;
			}
		});
		sw::timeit("threshold", [&]() {
			forxy(img) {
				auto& c=img(p);
				c = ci::constrain(c, 0.0f, 1.0f);
				auto c2 = 3.0f*c*c-2.0f*c*c*c;
				c = mix(c, c2, contrastizeFactor);
			}
		});
		//forxy(img) img(p)=max(0.0f, min(1.0f, img(p)));
		//mm(img, "img");
		return img;
	}
	Img multiscaleApply(Img src, function<Img(Img)> func) {
		int size = min(src.w, src.h);
		auto state = src.clone();
		vector<Img> scales;
		auto filter=ci::FilterGaussian();
		while(size > 1)
		{
			scales.push_back(state);
			state = ::resize(state, state.Size() / 2, filter);
			size /= 2;
		}
		vector<Img> origScales=scales;
		foreach(auto& s, origScales) s = s.clone();
		int lastLevel = 0;
		for(int i = scales.size() - 1; i >= lastLevel; i--) {
			//texs[i] = gtex(scales[i]);
			auto& thisScale = scales[i];
			auto& thisOrigScale = origScales[i];
			auto transformed = func(thisScale);
			auto diff = empty_like(transformed);
			sw::timeit("::map", [&]() {
#pragma omp parallel for
				for(int j = 0; j < diff.area; j++) {
					diff(j) = transformed(j) - thisOrigScale(j);
				}
			});
			float w = 1.0f-pow(i/float(scales.size()-1), 10.0f);
			w = max(0.0f, min(1.0f, w));
			sw::timeit("2 loops", [&]() {
				forxy(diff) {
					diff(p) *= w;
				}
			});
			if(i == lastLevel)
			{
				sw::timeit("::map", [&]() {
#pragma omp parallel for
					for (int j = 0; j < transformed.area; j++) {
						scales[lastLevel](j) = thisOrigScale(j) + diff(j);//.clone();
					}
				});
				break;
			}
			auto& nextScaleUp = scales[i-1];
			//texs[i] = gtex(::resize(transformed, nextScaleUp.Size(), filter));
			auto upscaledDiff = ::resize(diff, nextScaleUp.Size(), filter);
			sw::timeit("2 loops", [&]() {
				forxy(nextScaleUp) {
					nextScaleUp(p) += upscaledDiff(p);
				}
			});
		}
		return scales[lastLevel];
	}

	void stefanUpdate() {
		if(pause2) {
			return;
		}
		img = multiscaleApply(img, update_1_scale);
		//img = update_1_scale(img);
	}
	
	void stefanDraw()
	{
		gl::clear(Color(0, 0, 0));
		cout <<"frame# "<<getElapsedFrames()<<endl;

		gl::setMatricesWindowPersp(vec2(sx, sy), 90.0f, 1.0f, 1000.0f, false);
		//gl::setMatricesWindow(vec2(sx, sy), false);
		//gl::clearDepth(0.0f);
		gl::clear(ColorA::black(), true);
		//gl::disableDepthRead();
		gl::enableDepth();
		
		sw::timeit("draw", [&]() {
			if(1) {
				renderer.render(img);
				//auto tex = gtex(img);
				//gl::draw(redToLuminance(tex), getWindowBounds());
			} else {
				vector<gl::TextureRef> ordered;
				do {
					foreach(auto& pair, texs) {
						ordered.push_back(pair.second);
					}
				}while(0);
		
				float my=max(0.0f,min(1.0f,mouseY));
				int i=(texs.size()-1)*my;
				auto tex=ordered[i];
				tex->bind();
				//tex.setMagFilter(GL_NEAREST);
				gl::draw(redToLuminance(tex), getWindowBounds());
			}
		});
	}
};

CINDER_APP(SApp, RendererGl)