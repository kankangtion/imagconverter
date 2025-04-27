# imagconverter
需要安装ImageMagick-7.1.1-47-Q16-HDRI-x64-dll.exe，安装时勾选下面选项
![image](https://github.com/kankangtion/imagconverter/blob/master/images/1745743731015.png)

# Visual Studio 工程配置
配置项目属性，头文件附加包含目录是 ImageMagick 安装文件夹下的 include 目录：
![image](https://github.com/kankangtion/imagconverter/blob/master/images/1745743846553.jpg)

库文件的目录是 ImageMagick 安装文件夹下的lib目录：
![image](https://github.com/kankangtion/imagconverter/blob/master/images/1745743914329.jpg)

同时附加依赖项要把 lib 目录下的 .lib 文件填进去：
![image](https://github.com/kankangtion/imagconverter/blob/master/images/1745743949460.jpg)


# imagconverter操作使用
![image](https://github.com/kankangtion/imagconverter/blob/master/images/1745744015093.jpg)
1、点击文件选择导入图片这里可以选择多个图片，未配置预设后缀格式可以选择All Files .*
2、选择需要导出的图片格式目前预设的格式只要常见的12种可以修改代码自己任意添加剩余的200多种格式
3、点击开始转换前需要在设置里面选择输出的自定义目录，然后在点击开始转换

imagconverter工具转换图片用了多线程，批量测试了200多张图片秒转换成功，磁盘IO与CPU读写速度看你的电脑性能了。
