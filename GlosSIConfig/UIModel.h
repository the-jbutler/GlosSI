/*
Copyright 2021 Peter Repukat - FlatspotSoftware

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once
#include <filesystem>
#include <QObject>

class UIModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isWindows READ getIsWindows CONSTANT)

public:
    UIModel();

    Q_INVOKABLE QStringList getTargetList() const;

    bool getIsWindows() const;

private:
    std::filesystem::path config_path_;
    QString config_dir_name_;
#ifdef _WIN32
    bool is_windows_ = true;
#else
    bool is_windows_ = false;
#endif
};
