/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/skybrowser/include/browser.h>

#include <modules/webbrowser/webbrowsermodule.h>
#include <modules/webbrowser/include/webkeyboardhandler.h>
#include <modules/webbrowser/include/browserinstance.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <ghoul/opengl/texture.h>
#include <ghoul/logging/logmanager.h>

namespace {
    constexpr const char* _loggerCat = "Browser";

    const openspace::properties::Property::PropertyInfo DimensionsInfo = {
        "Dimensions",
        "Browser Dimensions",
        "Set the dimensions of the web browser window."
    };

    const openspace::properties::Property::PropertyInfo UrlInfo = {
        "Url",
        "URL",
        "The URL to load"
    };

    const openspace::properties::Property::PropertyInfo ReloadInfo = {
        "Reload",
        "Reload",
        "Reload the web browser"
    };

    struct [[codegen::Dictionary(Browser)]] Parameters {
        // [[codegen::verbatim(DimensionsInfo.description)]]
        std::optional<glm::vec2> dimensions;

        // [[codegen::verbatim(UrlInfo.description)]]
        std::optional<std::string> url;

        // [[codegen::verbatim(ReloadInfo.description)]]
        std::optional<bool> reload;
    };

#include "browser_codegen.cpp"

} // namespace

namespace openspace {

void Browser::RenderHandler::draw() {}

void Browser::RenderHandler::render() {}

void Browser::RenderHandler::setTexture(GLuint t) {
    _texture = t;
}

Browser::Browser(const ghoul::Dictionary& dictionary)
    : _url(UrlInfo)
    , _browserPixeldimensions(
        DimensionsInfo,
        glm::vec2(500.f),
        glm::vec2(10.f),
        glm::vec2(3000.f)
    )
    , _reload(ReloadInfo)
{
    if (dictionary.hasValue<std::string>(UrlInfo.identifier)) {
        _url = dictionary.value<std::string>(UrlInfo.identifier);
    }
    // Handle target dimension property
    const Parameters p = codegen::bake<Parameters>(dictionary);
    _url = p.url.value_or(_url);
    _browserPixeldimensions = p.dimensions.value_or(_browserPixeldimensions);

    glm::vec2 windowDimensions = global::windowDelegate->currentSubwindowSize();
    _browserPixeldimensions = windowDimensions;

    _url.onChange([this]() { _isUrlDirty = true; });
    _browserPixeldimensions.onChange([this]() { _isDimensionsDirty = true; });
    _reload.onChange([this]() { _shouldReload = true; });

    // Create browser and render handler
    _renderHandler = new RenderHandler();
    _keyboardHandler = new WebKeyboardHandler();
    _browserInstance = std::make_unique<BrowserInstance>(
        _renderHandler.get(),
        _keyboardHandler.get()
    );

    WebBrowserModule* webBrowser = global::moduleEngine->module<WebBrowserModule>();
    if (webBrowser) {
        webBrowser->addBrowser(_browserInstance.get());
    }
}

Browser::~Browser() {}

bool Browser::initializeGL() {
    _texture = std::make_unique<ghoul::opengl::Texture>(
        glm::uvec3(glm::ivec2(_browserPixeldimensions.value()), 1),
        GL_TEXTURE_2D
    );

    _renderHandler->setTexture(*_texture);

    _browserInstance->initialize();
    _browserInstance->loadUrl(_url);
    return isReady();
}

bool Browser::deinitializeGL() {
    _renderHandler->setTexture(0);

    _texture = nullptr;

    LDEBUG(fmt::format("Deinitializing browser: {}", _url.value()));

    _browserInstance->close(true);

    WebBrowserModule* webBrowser = global::moduleEngine->module<WebBrowserModule>();
    if (webBrowser) {
        webBrowser->removeBrowser(_browserInstance.get());
        _browserInstance.reset();
    }
    else {
        LWARNING("Could not find WebBrowserModule");
    }

    return true;
}

void Browser::render() {
    if (!_renderHandler->isTextureReady()) {
        return;
    }
    _renderHandler->updateTexture();
}

void Browser::update() {
    if (_isUrlDirty) {
        _browserInstance->loadUrl(_url);
        _isUrlDirty = false;
    }
    if (_isDimensionsDirty) {
        if (_browserPixeldimensions.value().x > 0 &&
            _browserPixeldimensions.value().y > 0)
        {
            _browserInstance->reshape(_browserPixeldimensions.value());
            _isDimensionsDirty = false;
        }
    }

    if (_shouldReload) {
        _browserInstance->reloadBrowser();
        _shouldReload = false;
    }
}

bool Browser::isReady() const {
    return _texture.get();
}

glm::vec2 Browser::browserPixelDimensions() const {
    return _browserPixeldimensions.value();
}

// Updates the browser size to match the size of the texture
void Browser::updateBrowserSize() {
    _browserPixeldimensions = _texture->dimensions();
}

float Browser::browserRatio() const {
    return static_cast<float>(_texture->dimensions().x) /
        static_cast<float>(_texture->dimensions().y);
}

void Browser::setCallbackDimensions(
                                const std::function<void(const glm::dvec2&)>& function)
{
    _browserPixeldimensions.onChange([&]() {
        function(_browserPixeldimensions.value());
    });
}

void Browser::executeJavascript(const std::string& script) const {
    // Make sure that the browser has a main frame
    const bool browserExists = _browserInstance && _browserInstance->getBrowser();
    const bool frameIsLoaded = browserExists &&
        _browserInstance->getBrowser()->GetMainFrame();

    if (frameIsLoaded) {
        _browserInstance->getBrowser()->GetMainFrame()->ExecuteJavaScript(
            script,
            _browserInstance->getBrowser()->GetMainFrame()->GetURL(),
            0
        );
    }
}
} // namespace openspace
