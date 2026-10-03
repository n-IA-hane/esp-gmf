// Host fixture: production function bodies are inserted by the Python test.
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using esp_gmf_err_t = int;
using esp_gmf_job_err_t = int;
using esp_gmf_audio_element_handle_t = void *;
using esp_gmf_element_handle_t = void *;
using esp_gmf_port_handle_t = void *;
constexpr int ESP_GMF_ERR_OK = 0, ESP_GMF_ERR_INVALID_ARG = -2, ESP_GMF_JOB_ERR_OK = 0, ESP_GMF_JOB_ERR_FAIL = -1,
              ESP_GMF_JOB_ERR_CONTINUE = 1, ESP_GMF_JOB_ERR_DONE = 2, ESP_GMF_IO_ABORT = -3, ESP_GMF_MAX_DELAY = -1;
struct afe_fetch_result_t {
    int16_t *data;
    int data_size;
};
using esp_gmf_afe_output_cb_t = void (*)(const int16_t *, size_t, void *);
struct esp_gmf_afe_manager_features_t {
    bool wakeup{}, vad{};
};
struct esp_gmf_afe_cfg_t {
    void *afe_manager{};
    uint32_t delay_samples{};
    bool vcmd_detect_en{true};
};
struct DB {
    std::vector<uint8_t> bytes;
};
struct esp_gmf_payload_t {
    uint8_t *buf{};
    uint32_t valid_size{};
    bool is_done{};
};
struct esp_gmf_data_bus_block_t {
    uint8_t *buf{};
    uint32_t buf_length{}, valid_size{};
};
struct Element {
    void *in{}, *out{};
    struct {
        int data_size{6144};
    } in_attr;
};
struct esp_gmf_afe_t {
    Element element;
    esp_gmf_afe_cfg_t *cfg{};
    DB *in_db{}, *out_db{};
    esp_gmf_afe_output_cb_t output_cb{};
    void *output_ctx{};
};
#define OBJ_GET_CFG(p) (static_cast<esp_gmf_afe_t *>(p)->cfg)
#define ESP_GMF_ELEMENT_GET(p) (&static_cast<esp_gmf_afe_t *>(p)->element)
#define ESP_LOGE(...) ((void)0)
#define ESP_GMF_RET_ON_FAIL(tag, call, block, ...)                                                                     \
    do {                                                                                                               \
        if ((call) < 0)                                                                                                \
            block;                                                                                                     \
    } while (0)
int vad_events = 0, mn_events = 0, out_db_calls = 0, input_release = 0;
std::vector<uint8_t> sink;
std::mutex mutex;
std::condition_variable cv;
bool entered = false, ready = false;
uint8_t input_bytes[6144]{}, output_bytes[8192]{};
esp_gmf_payload_t input{input_bytes, 6144, false}, output{output_bytes, 0, false};
void esp_gmf_afe_manager_get_features(void *, esp_gmf_afe_manager_features_t *f) { f->vad = true; }
void wakeup_afe_monitor(afe_fetch_result_t *, void *) { ++vad_events; }
void mn_afe_monitor(afe_fetch_result_t *, void *) { ++mn_events; }
void esp_gmf_afe_cmd_handler(void *) {}
int esp_gmf_db_get_available(DB *d, uint32_t *n) {
    assert(d);
    ++out_db_calls;
    *n = 16384 - d->bytes.size();
    return 0;
}
int esp_gmf_db_get_filled_size(DB *d, uint32_t *n) {
    assert(d);
    ++out_db_calls;
    *n = d->bytes.size();
    return 0;
}
int esp_gmf_db_acquire_write(DB *d, esp_gmf_data_bus_block_t *, size_t, int) {
    assert(d);
    return 0;
}
int esp_gmf_db_release_write(DB *d, esp_gmf_data_bus_block_t *p, int) {
    d->bytes.insert(d->bytes.end(), p->buf, p->buf + p->valid_size);
    return 0;
}
int esp_gmf_db_acquire_read(DB *d, esp_gmf_data_bus_block_t *p, size_t n, int) {
    assert(d && d->bytes.size() >= n);
    if (p->buf)
        memcpy(p->buf, d->bytes.data(), n);
    p->valid_size = n;
    return 0;
}
int esp_gmf_db_release_read(DB *d, esp_gmf_data_bus_block_t *p, int) {
    d->bytes.erase(d->bytes.begin(), d->bytes.begin() + p->valid_size);
    return 0;
}
int esp_gmf_port_acquire_in(void *, esp_gmf_payload_t **p, int, int) {
    std::unique_lock lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [] { return ready; });
    *p = &input;
    return 0;
}
void esp_gmf_port_release_in(void *, esp_gmf_payload_t *, int) { ++input_release; }
void esp_gmf_port_acquire_out(void *, esp_gmf_payload_t **p, uint32_t, int) { *p = &output; }
void esp_gmf_port_release_out(void *, esp_gmf_payload_t *p, int) {
    sink.insert(sink.end(), p->buf, p->buf + p->valid_size);
}

// INSERT_PRODUCTION_FUNCTIONS_HERE
void immediate(const int16_t *data, size_t bytes, void *ctx) {
    assert(ctx == &sink);
    assert(vad_events == 1 && mn_events == 1);
    auto p = reinterpret_cast<const uint8_t *>(data);
    sink.insert(sink.end(), p, p + bytes);
}
int main() {
    assert(esp_gmf_afe_set_output_callback(nullptr, immediate, &sink) == ESP_GMF_ERR_INVALID_ARG);
    for (bool direct : {false, true}) {
        vad_events = mn_events = out_db_calls = input_release = 0;
        sink.clear();
        entered = ready = false;
        esp_gmf_afe_cfg_t cfg;
        esp_gmf_afe_t afe;
        afe.cfg = &cfg;
        DB in, out;
        cfg.delay_samples = 1;
        assert(esp_gmf_afe_set_output_callback(&afe, immediate, &sink) == ESP_GMF_ERR_INVALID_ARG);
        cfg.delay_samples = 0;
        assert(esp_gmf_afe_set_output_callback(&afe, direct ? immediate : nullptr, &sink) == 0);
        afe.in_db = &in;
        afe.out_db = direct ? nullptr : &out;
        assert(esp_gmf_afe_set_output_callback(&afe, nullptr, nullptr) == ESP_GMF_ERR_INVALID_ARG);
        int job = -99;
        std::thread input_job([&] { job = esp_gmf_afe_proc(&afe, nullptr); });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [] { return entered; });
        }
        int16_t pcm[512];
        for (int i = 0; i < 512; i++)
            pcm[i] = i - 256;
        afe_fetch_result_t result{pcm, sizeof(pcm)};
        esp_gmf_afe_result_proc(&result, &afe);
        assert(vad_events == 1 && mn_events == 1);
        if (direct) {
            assert(sink.size() == sizeof(pcm));
            assert(!memcmp(sink.data(), pcm, sizeof(pcm)));
            assert(out_db_calls == 0);
        } else {
            assert(sink.empty());
            assert(out.bytes.size() == sizeof(pcm));
        }
        result.data_size = 0;
        esp_gmf_afe_result_proc(&result, &afe);
        assert(vad_events == 1 && mn_events == 1);
        {
            std::lock_guard lock(mutex);
            ready = true;
        }
        cv.notify_all();
        input_job.join();
        assert(job == ESP_GMF_JOB_ERR_OK && input_release == 1);
        assert(in.bytes.size() == 6144);
        assert(sink.size() == sizeof(pcm) && !memcmp(sink.data(), pcm, sizeof(pcm)));
        if (direct)
            assert(out_db_calls == 0);
    }
    std::cout << "PASS: ready audio delivered while input blocked; legacy output preserved; no duplicate delivery; "
                 "VAD/command monitoring retained; invalid binding rejected\n";
}
