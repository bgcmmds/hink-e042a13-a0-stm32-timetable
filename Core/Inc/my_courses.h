/**
  ******************************************************************************
  * @file    my_courses.h
  * @brief   课程表数据入口（改课表只动这个文件）
  ******************************************************************************
  */
#ifndef __MY_COURSES_H
#define __MY_COURSES_H

/* 把本周课表填进全局课表实例（TT_AddCourse 的封装）。
 * 调用时机：EPD_Init() 之后、TT_Show() 之前。 */
void MyCourses_Load(void);

#endif /* __MY_COURSES_H */
