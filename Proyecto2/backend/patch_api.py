import re

with open('backend/src/main.cpp', 'r') as f:
    text = f.read()

new_endpoints = """
    svr.Get("/api/tree", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        std::map<std::string, std::string> params;
        params["path"] = req.has_param("path") ? req.get_param_value("path") : "/";
        std::string tree = cmd_find(params);
        json out; out["tree"] = tree;
        res.set_content(out.dump(), "application/json");
    });
    
    svr.Get("/api/journal", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        std::map<std::string, std::string> params;
        if(req.has_param("id")) params["id"] = req.get_param_value("id");
        std::string j = cmd_journaling(params);
        json out; out["journal"] = j;
        res.set_content(out.dump(), "application/json");
    });
"""

text = text.replace('svr.listen("0.0.0.0", 8080);', new_endpoints + '\n    svr.listen("0.0.0.0", 8080);')

with open('backend/src/main.cpp', 'w') as f:
    f.write(text)
