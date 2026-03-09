# Git 使用指南

本文档介绍 Trace Vector 项目的 Git 协作流程。

## 初次克隆项目

```bash
git clone https://github.com/META-Xiao/Trace-Vector.git
cd Trace-Vector
```

克隆后需要配置本地开发环境：
1. 复制 `.vscode/settings.example.json` 为 `.vscode/settings.json`
2. 修改其中的 Keil 路径为你本地的安装路径

## 基本工作流程

### 1. 开始工作前，拉取最新代码

```bash
git pull origin main
```

### 2. 创建功能分支（推荐）

```bash
git checkout -b feature/your-feature-name
```

分支命名规范：
- `feature/xxx` - 新功能
- `fix/xxx` - Bug 修复
- `docs/xxx` - 文档更新
- `refactor/xxx` - 代码重构

### 3. 提交代码

```bash
# 查看修改的文件
git status

# 添加文件到暂存区
git add .                    # 添加所有修改
git add file1.c file2.h      # 添加指定文件

# 提交
git commit -m "描述你的修改"
```

提交信息规范：
- 简洁明了，说明做了什么
- 中文或英文均可
- 例如：`添加电机控制模块` 或 `Add motor control module`

### 4. 推送到远程

```bash
# 推送到当前分支
git push origin feature/your-feature-name

# 如果是主分支
git push origin main
```

### 5. 合并到主分支

在 GitHub 上创建 Pull Request，经过代码审查后合并。

或者本地合并：
```bash
git checkout main
git merge feature/your-feature-name
git push origin main
```

## 常用命令

### 查看状态和历史

```bash
git status              # 查看当前状态
git log                 # 查看提交历史
git log --oneline       # 简洁查看历史
git diff                # 查看未暂存的修改
git diff --staged       # 查看已暂存的修改
```

### 撤销操作

```bash
# 撤销工作区的修改（未 add）
git checkout -- file.c

# 撤销暂存区的文件（已 add，未 commit）
git reset HEAD file.c

# 撤销最后一次提交（保留修改）
git reset --soft HEAD~1

# 撤销最后一次提交（丢弃修改）
git reset --hard HEAD~1
```

### 分支管理

```bash
git branch                    # 查看本地分支
git branch -a                 # 查看所有分支（包括远程）
git branch -d branch-name     # 删除本地分支
git push origin --delete branch-name  # 删除远程分支
```

### 解决冲突

当 `git pull` 或 `git merge` 出现冲突时：

1. 打开冲突文件，查找 `<<<<<<<`、`=======`、`>>>>>>>` 标记
2. 手动编辑，保留需要的代码
3. 删除冲突标记
4. 提交解决后的文件：
   ```bash
   git add conflicted-file.c
   git commit -m "解决合并冲突"
   ```

## 注意事项

### 不要提交的文件

以下文件已在 `.gitignore` 中配置，不会被提交：
- `.vscode/settings.json` - 个人配置
- `project/mdk/Out_File/` - 编译输出
- `*.uvopt` - Keil 个人配置
- 其他临时文件

### 提交前检查

```bash
# 确保代码能编译通过
# 检查是否误提交了个人配置或编译输出
git status
```

### 团队协作建议

1. 经常 `git pull` 保持代码最新
2. 提交前先拉取，避免冲突
3. 提交信息要清晰
4. 大功能使用分支开发
5. 不要直接在 `main` 分支上做实验性修改

## 常见问题

### Q: 忘记拉取就提交了，push 失败怎么办？

```bash
git pull --rebase origin main
git push origin main
```

### Q: 不小心提交了敏感文件怎么办？

```bash
# 从 Git 历史中删除文件
git rm --cached .vscode/settings.json
git commit -m "移除敏感文件"
git push origin main
```

### Q: 想放弃所有本地修改，恢复到远程版本？

```bash
git fetch origin
git reset --hard origin/main
```

## 更多资源

- [Git 官方文档](https://git-scm.com/doc)
- [GitHub 帮助文档](https://docs.github.com/)
- [Git 可视化学习](https://learngitbranching.js.org/)

