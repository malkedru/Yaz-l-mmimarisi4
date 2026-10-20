#ifndef FILELINESOURCE_H
#define FILELINESOURCE_H

#include "Source.h"
#include "Stage.h"
#include <fstream>
#include <string>

class FileLineSource : public Source<std::string> {
private:
    std::string filePath;

public:
    FileLineSource(const std::string& path) : filePath(path) {}

    void produce(Emitter<std::string>& out) override {
        std::ifstream file(filePath);

        if (!file.is_open()) {
            throw StageException("Dosya açılamadı: " + filePath);
        }

        std::string line;
        while (std::getline(file, line)) {
            out.emit(line);
        }
        if (file.bad()) throw StageException("Girdi dosyasi okunamadi: " + filePath);

        file.close();
    }
};

#endif
