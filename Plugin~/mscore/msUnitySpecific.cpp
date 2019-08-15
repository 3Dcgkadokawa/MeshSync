#include "pch.h"
#include "MeshUtils/MeshUtils.h"
#include "MeshSync/MeshSync.h"
#include "msCoreAPI.h"

using namespace mu;


struct Keyframe_R
{
    float time;
    float value;
    float in_tangent;
    float out_tangent;

    int   getTangentMode() const { return 0; }
    void  setTangentMode(int v) {}

    int   getWeightedMode() const { return 0; }
    void  setWeightedMode(int v) {}
    float getInWeight() const { return 0.0f; }
    void  setInWeight(float v) {}
    float getOutWeight() const { return 0.0f; }
    void  setOutWeight(float v) {}
};

struct Keyframe_E
{
    float time;
    float value;
    float in_tangent;
    float out_tangent;

    int tangent_mode; // editor only

    int   getTangentMode() const { return tangent_mode; }
    void  setTangentMode(int v) { tangent_mode = v; }

    int   getWeightedMode() const { return 0; }
    void  setWeightedMode(int v) {}
    float getInWeight() const { return 0.0f; }
    void  setInWeight(float v) {}
    float getOutWeight() const { return 0.0f; }
    void  setOutWeight(float v) {}
};

struct Keyframe_RW
{
    float time;
    float value;
    float in_tangent;
    float out_tangent;

    int weighted_mode;
    float in_weight;
    float out_weight;

    int   getTangentMode() const { return 0; }
    void  setTangentMode(int v) {}

    int   getWeightedMode() const { return weighted_mode; }
    void  setWeightedMode(int v) { weighted_mode = v; }
    float getInWeight() const { return in_weight; }
    void  setInWeight(float v) { in_weight = v; }
    float getOutWeight() const { return out_weight; }
    void  setOutWeight(float v) { out_weight = v; }
};

struct Keyframe_EW
{
    float time;
    float value;
    float in_tangent;
    float out_tangent;

    int tangent_mode; // editor only

    int weighted_mode;// 
    float in_weight;  // 
    float out_weight; // Unity 2018.1-?

    int   getTangentMode() const { return tangent_mode; }
    void  setTangentMode(int v) { tangent_mode = v; }

    int   getWeightedMode() const { return weighted_mode; }
    void  setWeightedMode(int v) { weighted_mode = v; }
    float getInWeight() const { return in_weight; }
    void  setInWeight(float v) { in_weight = v; }
    float getOutWeight() const { return out_weight; }
    void  setOutWeight(float v) { out_weight = v; }
};

// thanks: http://techblog.sega.jp/entry/2016/11/28/100000
template<class KF>
class AnimationCurveKeyReducer
{
public:
    static RawVector<KF> apply(const IArray<KF>& keys, float eps)
    {
        RawVector<KF> ret;
        ret.reserve(keys.size());
        if (keys.size() <= 2)
            ret.assign(keys.begin(), keys.end());
        else
            generate(keys, eps, ret);
        return ret;
    }

private:
    static void generate(const IArray<KF>& keys, float eps, RawVector<KF>& result)
    {
        int end = (int)keys.size() - 1;
        for (int k = 0, i = 1; i < end; i++) {
            if (!isInterpolationValue(keys[k], keys[i + 1], keys[i], eps)) {
                result.push_back(keys[k]);
                k = i;
            }
        }
        result.push_back(keys.back());
    }

    static bool isInterpolationValue(const KF& key1, const KF& key2, const KF& comp, float eps)
    {
        float val1 = getValue(key1, key2, comp.time);
        if (eps < std::abs(comp.value - val1))
            return false;

        float time = key1.time + (comp.time - key1.time) * 0.5f;
        val1 = getValue(key1, comp, time);
        float val2 = getValue(key1, key2, time);

        return std::abs(val2 - val1) <= eps ? true : false;
    }

    static float getValue(const KF& key1, const KF& key2, float time)
    {
        if (key1.out_tangent == std::numeric_limits<float>::infinity())
            return key1.value;

        float kd = key2.time - key1.time;
        float vd = key2.value - key1.value;
        float t = (time - key1.time) / kd;

        float a = -2 * vd + kd * (key1.out_tangent + key2.in_tangent);
        float b = 3 * vd - kd * (2 * key1.out_tangent + key2.in_tangent);
        float c = kd * key1.out_tangent;

        return key1.value + t * (t * (a * t + b) + c);
    }
};


enum InterpolationMode
{
    Smooth,
    Linear,
    Constant,
};
enum TangentMode
{
    kTangentModeFree = 0,
    kTangentModeAuto = 1,
    kTangentModeLinear = 2,
    kTangentModeConstant = 3,
    kTangentModeClampedAuto = 4,
};
const int kBrokenMask = 1 << 0;
const int kLeftTangentMask = 1 << 1 | 1 << 2 | 1 << 3 | 1 << 4;
const int kRightTangentMask = 1 << 5 | 1 << 6 | 1 << 7 | 1 << 8;
const float kTimeEpsilon = 0.00001f;
const float kDefaultWeight = 1.0f / 3.0f;
const float kCurveTimeEpsilon = 0.00001f;

template<class KF>
static inline float LinearTangent(IArray<KF>& curve, int i1, int i2)
{
    const auto& k1 = curve[i1];
    const auto& k2 = curve[i2];

    float dt = k2.time - k1.time;
    if (std::abs(dt) < kTimeEpsilon)
        return 0.0f;

    return (k2.value - k1.value) / dt;
}

template<class KF>
static inline TangentMode GetLeftTangentMode(const KF& key)
{
    return static_cast<TangentMode>((key.getTangentMode() & kLeftTangentMask) >> 1);
}

template<class KF>
static inline TangentMode GetRightTangentMode(const KF& key)
{
    return static_cast<TangentMode>((key.getTangentMode() & kRightTangentMask) >> 5);
}

static inline float SafeDiv(float y, float x)
{
    if (std::abs(x) > kCurveTimeEpsilon)
        return y / x;
    else
        return 0;
}

template<class KF>
static inline void SmoothTangents(IArray<KF>& curve, int index, float bias)
{
    if (curve.size() < 2)
        return;

    auto& key = curve[index];
    if (index == 0) {
        key.in_tangent = key.out_tangent = 0;

        if (curve.size() > 1) {
            key.setInWeight(kDefaultWeight);
            key.setOutWeight(kDefaultWeight);
        }
    }
    else if (index == curve.size() - 1) {
        key.in_tangent = key.out_tangent = 0;

        if (curve.size() > 1) {
            key.setInWeight(kDefaultWeight);
            key.setOutWeight(kDefaultWeight);
        }
    }
    else {
        float dx1 = key.time - curve[index - 1].time;
        float dy1 = key.value - curve[index - 1].value;

        float dx2 = curve[index + 1].time - key.time;
        float dy2 = curve[index + 1].value - key.value;

        float dx = dx1 + dx2;
        float dy = dy1 + dy2;

        float m1 = SafeDiv(dy1, dx1);
        float m2 = SafeDiv(dy2, dx2);

        float m = SafeDiv(dy, dx);

        if ((m1 > 0 && m2 > 0) || (m1 < 0 && m2 < 0)) {
            float lower_bias = (1.0f - bias) * 0.5f;
            float upper_bias = lower_bias + bias;

            float lower_dy = dy * lower_bias;
            float upper_dy = dy * upper_bias;

            if (std::abs(dy1) >= std::abs(upper_dy))
            {
                float b = SafeDiv(dy1 - upper_dy, lower_dy);
                //T mp = b * m2 + (1.0F - b) * m;
                float mp = (1.0F - b) * m;

                key.in_tangent = mp; key.out_tangent = mp;
            }
            else if (std::abs(dy1) < std::abs(lower_dy)) {
                float b = SafeDiv(dy1, lower_dy);
                //T mp = b * m + (1.0F - b) * m1;
                float mp = b * m;

                key.in_tangent = mp; key.out_tangent = mp;
            }
            else {
                key.in_tangent = m; key.out_tangent = m;
            }
        }
        else {
            key.in_tangent = key.out_tangent = 0.0f;
        }

        key.setInWeight(kDefaultWeight);
        key.setOutWeight(kDefaultWeight);
    }
}

template<class KF>
static inline void UpdateTangents(IArray<KF>& curve, int index)
{
    auto& key = curve[index];

    // linear
    if (GetLeftTangentMode(key) == kTangentModeLinear && index >= 1)
        key.in_tangent = LinearTangent(curve, index, index - 1);
    if (GetRightTangentMode(key) == kTangentModeLinear && index + 1 < curve.size())
        key.out_tangent = LinearTangent(curve, index, index + 1);

    // smooth
    if (GetLeftTangentMode(key) == kTangentModeClampedAuto || GetRightTangentMode(key) == kTangentModeClampedAuto)
        SmoothTangents(curve, index, 0.5f);

    // constant
    if (GetLeftTangentMode(key) == kTangentModeConstant)
        key.in_tangent = std::numeric_limits<float>::infinity();
    if (GetRightTangentMode(key) == kTangentModeConstant)
        key.out_tangent = std::numeric_limits<float>::infinity();
}

template<class KF>
static void SetTangentMode(KF *key, int n, InterpolationMode im)
{
    TangentMode tangent_mode;
    switch (im) {
    case Linear: tangent_mode = kTangentModeLinear; break;
    case Constant: tangent_mode = kTangentModeConstant; break;
    default: tangent_mode = kTangentModeClampedAuto; break;
    }

    for (int i = 0; i < n; ++i) {
        auto& k = key[i];
        int tm = k.getTangentMode();
        tm |= kBrokenMask;
        tm &= ~kLeftTangentMask;
        tm |= (int)tangent_mode << 1;
        tm &= ~kRightTangentMask;
        tm |= (int)tangent_mode << 5;
        k.setTangentMode(tm);

        k.setInWeight(kDefaultWeight);
        k.setOutWeight(kDefaultWeight);
    }

    IArray<KF> curve{ key, (size_t)n };
    for (int i = 0; i < n; ++i)
        UpdateTangents(curve, i);
}

template<class KF>
static inline void FillCurve(const ms::TAnimationCurve<int>& src, void *x_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = (float)v.value;
    }
    SetTangentMode(x, n, it);
}

template<class KF>
static inline void FillCurve(const ms::TAnimationCurve<float>& src, void *x_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = v.value;
    }
    SetTangentMode(x, n, it);
}

template<class KF>
static inline void FillCurves(const ms::TAnimationCurve<float2>& src, void *x_, void *y_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    KF *y = (KF*)y_;
    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = v.value.x;
        y[i].time = v.time;
        y[i].value = v.value.y;
    }
    SetTangentMode(x, n, it);
    SetTangentMode(y, n, it);
}

template<class KF>
static inline void FillCurves(const ms::TAnimationCurve<float3>& src, void *x_, void *y_, void *z_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    KF *y = (KF*)y_;
    KF *z = (KF*)z_;

    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = v.value.x;
        y[i].time = v.time;
        y[i].value = v.value.y;
        z[i].time = v.time;
        z[i].value = v.value.z;
    }
    SetTangentMode(x, n, it);
    SetTangentMode(y, n, it);
    SetTangentMode(z, n, it);
}

template<class KF>
static inline void FillCurves(const ms::TAnimationCurve<float4>& src, void *x_, void *y_, void *z_, void *w_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    KF *y = (KF*)y_;
    KF *z = (KF*)z_;
    KF *w = (KF*)w_;

    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = v.value.x;
        y[i].time = v.time;
        y[i].value = v.value.y;
        z[i].time = v.time;
        z[i].value = v.value.z;
        w[i].time = v.time;
        w[i].value = v.value.w;
    }
    SetTangentMode(x, n, it);
    SetTangentMode(y, n, it);
    SetTangentMode(z, n, it);
    SetTangentMode(w, n, it);

}
template<class KF>
static inline void FillCurves(const ms::TAnimationCurve<quatf>& src, void *x_, void *y_, void *z_, void *w_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    KF *y = (KF*)y_;
    KF *z = (KF*)z_;
    KF *w = (KF*)w_;

    int n = (int)src.size();
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        x[i].time = v.time;
        x[i].value = v.value.x;
        y[i].time = v.time;
        y[i].value = v.value.y;
        z[i].time = v.time;
        z[i].value = v.value.z;
        w[i].time = v.time;
        w[i].value = v.value.w;
    }
    SetTangentMode(x, n, it);
    SetTangentMode(y, n, it);
    SetTangentMode(z, n, it);
    SetTangentMode(w, n, it);
}

template<class KF>
static inline void FillCurvesEuler(const ms::TAnimationCurve<quatf>& src, void *x_, void *y_, void *z_, InterpolationMode it)
{
    KF *x = (KF*)x_;
    KF *y = (KF*)y_;
    KF *z = (KF*)z_;

    int n = (int)src.size();
    float3 prev;
    for (int i = 0; i < n; ++i) {
        const auto v = src[i];
        auto r = mu::to_euler_zxy(v.value) * mu::RadToDeg;
        if (i > 0) {
            // make continuous
            auto d = r - mu::mod(prev, 360.0f);
            auto x0 = mu::abs(d);
            auto x1 = mu::abs(d - 360.0f);
            auto x2 = mu::abs(d + 360.0f);

            if (x1.x < x0.x)
                r.x -= 360.0f;
            else if (x2.x < x0.x)
                r.x += 360.0f;

            if (x1.y < x0.y)
                r.y -= 360.0f;
            else if (x2.y < x0.y)
                r.y += 360.0f;

            if (x1.z < x0.z)
                r.z -= 360.0f;
            else if (x2.z < x0.z)
                r.z += 360.0f;

            auto c = prev / 360.0f;
            c -= mu::frac(c);
            r += c * 360.0f;
        }
        prev = r;

        x[i].time = v.time;
        x[i].value = r.x;
        y[i].time = v.time;
        y[i].value = r.y;
        z[i].time = v.time;
        z[i].value = r.z;
    }
    SetTangentMode(x, n, it);
    SetTangentMode(y, n, it);
    SetTangentMode(z, n, it);
}

static int g_sizeof_keyframe = 0;

msAPI void msSetSizeOfKeyframe(int v)
{
    g_sizeof_keyframe = v;
}

#define Switch(Func, ...)\
    switch (g_sizeof_keyframe) {\
        case sizeof(Keyframe_EW): Func<Keyframe_EW>(__VA_ARGS__); break;\
        case sizeof(Keyframe_RW): Func<Keyframe_EW>(__VA_ARGS__); break;\
        case sizeof(Keyframe_E): Func<Keyframe_E>(__VA_ARGS__); break;\
        case sizeof(Keyframe_R): Func<Keyframe_R>(__VA_ARGS__); break;\
        default: break;\
    }\

static void ConvertCurve(ms::AnimationCurve& curve, InterpolationMode it)
{
    size_t data_size = g_sizeof_keyframe * curve.size();

    if (curve.data_flags.force_constant)
        it = InterpolationMode::Constant;

    char *dst[4]{};
    auto alloc = [&](int n) {
        curve.idata.resize(n);
        for (int i = 0; i < n; ++i) {
            curve.idata[i].resize_zeroclear(data_size);
            dst[i] = curve.idata[i].data();
        }
    };

    switch (curve.data_type) {
    case ms::Animation::DataType::Int:
        alloc(1);
        Switch(FillCurve, ms::TAnimationCurve<int>(curve), dst[0], it);
        break;
    case ms::Animation::DataType::Float:
        alloc(1);
        Switch(FillCurve, ms::TAnimationCurve<float>(curve), dst[0], it);
        break;
    case ms::Animation::DataType::Float2:
        alloc(2);
        Switch(FillCurves, ms::TAnimationCurve<float2>(curve), dst[0], dst[1], it);
        break;
    case ms::Animation::DataType::Float3:
        alloc(3);
        Switch(FillCurves, ms::TAnimationCurve<float3>(curve), dst[0], dst[1], dst[2], it);
        break;
    case ms::Animation::DataType::Float4:
        alloc(4);
        Switch(FillCurves, ms::TAnimationCurve<float4>(curve), dst[0], dst[1], dst[2], dst[3], it);
        break;
    case ms::Animation::DataType::Quaternion:
        if (it == InterpolationMode::Linear || it == InterpolationMode::Constant) {
            alloc(3);
            Switch(FillCurvesEuler, ms::TAnimationCurve<quatf>(curve), dst[0], dst[1], dst[2], it);
        }
        else {
            alloc(4);
            Switch(FillCurves, ms::TAnimationCurve<quatf>(curve), dst[0], dst[1], dst[2], dst[3], it);
        }
        break;
    }
}
static void ConvertCurves(ms::Animation& anim, InterpolationMode it)
{
    int n = (int)anim.curves.size();
    for (int i = 0; i != n; ++i)
        ConvertCurve(*anim.curves[i], it);
}

template<class KF>
static void KeyframeReductionImpl(RawVector<char>& idata, float threshold)
{
    if (idata.empty())
        return;

    auto *keys = (const KF*)idata.cdata();
    size_t key_count = idata.size() / sizeof(KF);

    auto new_keys = AnimationCurveKeyReducer<KF>().apply({keys, key_count}, threshold);
    auto ptr = (char*)new_keys.cdata();
    idata.assign(ptr, ptr + (sizeof(KF) * new_keys.size()));
}

static void KeyframeReduction(ms::AnimationCurve& curve, float threshold)
{
    for (auto& idata : curve.idata) {
        Switch(KeyframeReductionImpl, idata, threshold);
    }
}

static void KeyframeReduction(ms::Animation& anim, float threshold)
{
    int n = (int)anim.curves.size();
    for (int i = 0; i != n; ++i)
        KeyframeReduction(*anim.curves[i], threshold);
}

static void KeyframeReduction(ms::AnimationClip *self, float threshold)
{
    int n = (int)self->animations.size();
    mu::parallel_for_blocked(0, n, 16,
        [&](int begin, int end) {
            for (int i = begin; i != end; ++i)
                KeyframeReduction(*self->animations[i], threshold);
        });
}

msAPI int msCurveGetNumElements(ms::AnimationCurve *self)
{
    return (int)self->idata.size();
}

msAPI int msCurveGetNumKeys(ms::AnimationCurve *self, int i)
{
    return (int)self->idata[i].size() / g_sizeof_keyframe;
}

msAPI void msCurveCopy(ms::AnimationCurve *self, int i, void *dst)
{
    memcpy(dst, self->idata[i].cdata(), self->idata[i].size());
}

msAPI void msCurveConvert(ms::AnimationCurve *self, InterpolationMode it)
{
    ConvertCurve(*self, it);
}
msAPI void msCurveKeyframeReduction(ms::AnimationCurve *self, float threshold)
{
    KeyframeReduction(*self, threshold);
}

msAPI void msAnimationConvert(ms::Animation *self, InterpolationMode it)
{
    ConvertCurves(*self, it);
}
msAPI void msAnimationKeyframeReduction(ms::Animation *self, float threshold)
{
    KeyframeReduction(*self, threshold);
}

msAPI void msAnimationClipConvert(ms::AnimationClip *self, InterpolationMode it)
{
    int n = (int)self->animations.size();
    mu::parallel_for_blocked(0, n, 16,
        [&](int begin, int end) {
            for (int i = begin; i != end; ++i)
                ConvertCurves(*self->animations[i], it);
        });
}
msAPI void msAnimationClipKeyframeReduction(ms::AnimationClip *self, float threshold)
{
    int n = (int)self->animations.size();
    mu::parallel_for_blocked(0, n, 16,
        [&](int begin, int end) {
            for (int i = begin; i != end; ++i)
                KeyframeReduction(*self->animations[i], threshold);
        });
}
#undef Switch
