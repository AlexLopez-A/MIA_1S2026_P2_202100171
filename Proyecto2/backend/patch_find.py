import re

with open('src/file_manager.h', 'r') as f:
    content = f.read()

content = re.sub(
    r'std::filesystem::path physicalPath = "archivos" \+ startPath;',
    r'MountedPart* mp_phys = getLoggedMount();\n        std::string partFolder = mp_phys ? "archivos/" + std::string(mp_phys->id) : "archivos";\n        std::filesystem::path physicalPath = std::filesystem::path(partFolder) / (startPath.empty() ? "" : startPath.substr(1));',
    content
)

with open('src/file_manager.h', 'w') as f:
    f.write(content)

