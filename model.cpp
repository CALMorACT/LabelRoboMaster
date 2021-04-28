//
// Created by xinyang on 2021/4/28.
//

#include "model.hpp"
#include <QFile>

template<class F, class T, class ...Ts>
T reduce(F &&func, T x, Ts ...xs) {
    if constexpr (sizeof...(Ts) > 0) {
        return func(x, reduce(std::forward<F>(func), xs...));
    } else {
        return x;
    }
}

template<class T, class ...Ts>
T reduce_min(T x, Ts ...xs) {
    return reduce([](auto a, auto b) { return std::min(a, b); }, x, xs...);
}

template<class T, class ...Ts>
T reduce_max(T x, Ts ...xs) {
    return reduce([](auto a, auto b) { return std::max(a, b); }, x, xs...);
}

static inline bool is_overlap(const QPointF pts1[4], const QPointF pts2[4]) {
    cv::Rect2f box1, box2;
    box1.x = reduce_min(pts1[0].x(), pts1[1].x(), pts1[2].x(), pts1[3].x());
    box1.y = reduce_min(pts1[0].y(), pts1[1].y(), pts1[2].y(), pts1[3].y());
    box1.width = reduce_max(pts1[0].x(), pts1[1].x(), pts1[2].x(), pts1[3].x()) - box1.x;
    box1.height = reduce_max(pts1[0].y(), pts1[1].y(), pts1[2].y(), pts1[3].y()) - box1.y;
    box2.x = reduce_min(pts2[0].x(), pts2[1].x(), pts2[2].x(), pts2[3].x());
    box2.y = reduce_min(pts2[0].y(), pts2[1].y(), pts2[2].y(), pts2[3].y());
    box2.width = reduce_max(pts2[0].x(), pts2[1].x(), pts2[2].x(), pts2[3].x()) - box2.x;
    box2.height = reduce_max(pts2[0].y(), pts2[1].y(), pts2[2].y(), pts2[3].y()) - box2.y;
    return (box1 & box2).area() > 0;
}

static inline int argmax(const float *ptr, int len) {
    int max_arg = 0;
    for (int i = 1; i < len; i++) {
        if (ptr[i] > ptr[max_arg]) max_arg = i;
    }
    return max_arg;
}

constexpr float inv_sigmoid(float x) {
    return -std::log(1 / x - 1);
}

constexpr float sigmoid(float x) {
    return 1 / (1 + std::exp(-x));
}


SmartModel::SmartModel() {
    QFile xml_file(":/nn/resource/model-opt.xml");
    QFile bin_file(":/nn/resource/model-opt.bin");
    xml_file.open(QIODevice::ReadOnly);
    bin_file.open(QIODevice::ReadOnly);
    auto xml_bytes = xml_file.readAll();
    auto bin_bytes = bin_file.readAll();
    net = cv::dnn::readNetFromModelOptimizer((uint8_t*)xml_bytes.data(), xml_bytes.size(), (uint8_t*)bin_bytes.data(), bin_bytes.size());
}

void SmartModel::run(const QString &image_file, QVector<box_t> &boxes) {
    auto img = cv::imread(image_file.toStdString());
    float scale = 640.f / img.cols;
    cv::resize(img, img, {-1, -1}, scale, scale);
    auto x = cv::dnn::blobFromImage(img);
    net.setInput(x);
    auto y = net.forward();
    QVector<box_t> before_nms;
    for (int i = 0; i < y.size[1]; i++) {
        float *result = (float *) y.data + i * y.size[2];
        if (result[8] < inv_sigmoid(0.5)) continue;
        box_t box;
        for (int i = 0; i < 4; i++) {
            box.pts[i].rx() = result[i * 2 + 0];
            box.pts[i].ry() = result[i * 2 + 1];
        }
        for (auto &pt : box.pts) pt.rx() /= scale, pt.ry() /= scale;
        box.color_id = argmax(result + 9, 4);
        box.tag_id = argmax(result + 13, 7);
        box.conf = sigmoid(result[8]);
        before_nms.append(box);
    }
    std::sort(boxes.begin(), boxes.end(), [](box_t &b1, box_t &b2) {
        return b1.conf > b2.conf;
    });
    boxes.clear();
    boxes.reserve(before_nms.size());
    std::vector<bool> is_removed(before_nms.size());
    for (int i = 0; i < before_nms.size(); i++) {
        if (is_removed[i]) continue;
        boxes.append(before_nms[i]);
        for (int j = i + 1; j < before_nms.size(); j++) {
            if (is_removed[j]) continue;
            if (is_overlap(before_nms[i].pts, before_nms[j].pts)) is_removed[j] = true;
        }
    }
}