#pragma once
// =============================================================================
// llamacpp_impl.h — Обёртка llama.cpp для РСАД
//
//  Актуальное API llama.cpp b4000+ (апрель 2026):
//    - llama_model_load_from_file()   вместо llama_load_model_from_file()
//    - llama_sampler_chain_init()     вместо ручного сэмплинга
//    - llama_tokenize()               без изменений
//    - llama_batch_get_one()          без изменений
//
//  Компилируется только при RSAD_USE_LLAMACPP=ON
// =============================================================================

#ifdef RSAD_USE_LLAMACPP
#include "llama.h"
#include <string>
#include <vector>
#include <cstdio>

// ---------------------------------------------------------------------------
// Обёртка — один экземпляр на процесс
// ---------------------------------------------------------------------------
struct LlamaHandle {
    llama_model*   model   = nullptr;
    llama_context* ctx     = nullptr;
    llama_sampler* sampler = nullptr;
    bool           loaded  = false;

    bool load(const std::string& path, int nGpuLayers = 99) {
        llama_backend_init();

        // ── Загрузка модели ──────────────────────────────────────────────
        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = nGpuLayers;

        // Пробуем новое имя функции, потом старое
        model = llama_model_load_from_file(path.c_str(), mparams);
        if (!model) {
            fprintf(stderr, "[LlamaHandle] Failed to load: %s\n", path.c_str());
            return false;
        }

        // ── Контекст ─────────────────────────────────────────────────────
        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx     = 1024;
        cparams.n_batch   = 512;

        ctx = llama_new_context_with_model(model, cparams);
        if (!ctx) {
            fprintf(stderr, "[LlamaHandle] Failed to create context\n");
            llama_model_free(model); model = nullptr;
            return false;
        }

        // ── Сэмплер (новый chain API) ─────────────────────────────────────
        llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
        sampler = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(42));

        loaded = true;
        printf("[LlamaHandle] Model loaded: %s (GPU layers=%d)\n",
               path.c_str(), nGpuLayers);
        return true;
    }

    // ── Генерация текста ──────────────────────────────────────────────────
    std::string generate(const std::string& prompt, int maxTokens = 256) {
        if (!loaded || !ctx || !model) return "";

        // Vocab — отдельный объект в llama.cpp b4000+
        const llama_vocab* vocab = llama_model_get_vocab(model);

        // Токенизация
        std::vector<llama_token> tokens(prompt.size() + 16);
        int n = llama_tokenize(vocab,
                               prompt.c_str(), (int)prompt.size(),
                               tokens.data(), (int)tokens.size(),
                               /*add_special=*/true,
                               /*parse_special=*/false);
        if (n < 0) return "";
        tokens.resize(n);

        // Первый батч — промпт
        llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
        if (llama_decode(ctx, batch) != 0) return "";

        // Генерация
        std::string result;
        for (int i = 0; i < maxTokens; i++) {
            llama_token tok = llama_sampler_sample(sampler, ctx, -1);
            if (llama_token_is_eog(vocab, tok)) break;

            // Токен → текст
            char buf[64];
            int nb = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, false);
            if (nb > 0) result.append(buf, nb);

            // Следующий батч — одиночный токен
            llama_batch next = llama_batch_get_one(&tok, 1);
            if (llama_decode(ctx, next) != 0) break;

            // Сброс сэмплера между токенами
            llama_sampler_reset(sampler);
        }
        return result;
    }

    void unload() {
        if (sampler) { llama_sampler_free(sampler); sampler = nullptr; }
        if (ctx)     { llama_free(ctx);             ctx     = nullptr; }
        if (model)   { llama_model_free(model);     model   = nullptr; }
        llama_backend_free();
        loaded = false;
    }

    ~LlamaHandle() { unload(); }
};

#endif // RSAD_USE_LLAMACPP
